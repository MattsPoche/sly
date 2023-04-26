#include <assert.h>
#include "sly_types.h"
#include "parser.h"
#define OPCODES_INCLUDE_INLINE 1
#include "opcodes.h"
#include "sly_compile.h"
#include "eval.h"
#include "sly_vm.h"
#include "sly_alloc.h"

/* TODO [ ] Associate syntactic info with opcodes (semi-done)
 */

enum kw {
	kw_define = 0,
	kw_lambda,
	kw_quote,
	kw_syntax_quote,
	kw_begin,
	kw_if,
	kw_set,
	kw_define_syntax,
	kw_call_with_continuation,
	kw_call_cc,
	kw_include,
	KW_COUNT,
};

enum symbol_type {
	sym_datum = 0,
	sym_keyword,	// 1
	sym_variable,	// 2
	sym_arg,		// 3
	sym_upval,		// 4
	sym_global,		// 5
	sym_constant,	// 6
};

union symbol_properties {
	sly_value v;
	struct {
		u8 type;
		u8 islocal;
		u8 issyntax;
		u8 _pad[1];
		i32 reg;
	} p;
};

#define IS_GLOBAL(scope)       ((scope)->parent == NULL)
#define ADD_BUILTIN(name, fn, nargs, has_vargs)							\
	do {																\
		sym = make_symbol(ss, name, strlen(name));						\
		dictionary_set(ss, symtable, sym, st_prop.v);					\
		dictionary_set(ss, cc->globals, sym, make_cclosure(ss, fn, nargs, has_vargs)); \
	} while (0)
#define STORE_MACRO_CLOSURE()								\
	do {													\
		st_prop.p.issyntax = 1;								\
		dictionary_set(ss, symtable, var, st_prop.v);		\
		size_t len = vector_len(proto->K);					\
		sly_value p = vector_ref(proto->K, len-1);			\
		if (ss->eval_frame == NULL) {						\
			ss->eval_frame = make_eval_stack(ss);			\
			ss->eval_frame->U = make_vector(ss, 1, 1);		\
			vector_set(ss->eval_frame->U, 0, globals);		\
		}													\
		ss->frame = ss->eval_frame;							\
		sly_value clos = form_closure(ss, p);				\
		ss->eval_frame = ss->frame;							\
		dictionary_set(ss, cc->cscope->macros, var, clos);	\
	} while (0)


static char *
keywords(int idx)
{
	switch ((enum kw)idx) {
	case kw_define: return "define";
	case kw_lambda: return "lambda";
	case kw_quote: return "quote";
	case kw_syntax_quote: return "syntax-quote";
	case kw_set: return "set!";
	case kw_begin: return "begin";
	case kw_if: return "if";
	case kw_define_syntax: return "define-syntax";
	case kw_call_with_continuation: return "call-with-continuation";
	case kw_call_cc: return "call/cc";
	case kw_include: return "include";
	case KW_COUNT: break;
	}
	sly_assert(0, "Error, No such keyword");
	return NULL;
}

static sly_value kw_symbols[KW_COUNT];

int comp_expr(Sly_State *ss, sly_value form, int reg);
int comp_atom(Sly_State *ss, sly_value form, int reg);
static void init_symtable(Sly_State *ss, sly_value symtable);

int
is_keyword(sly_value sym)
{
	for (int i = 0; i < KW_COUNT; ++i) {
		if (symbol_eq(sym, kw_symbols[i])) {
			return i;
		}
	}
	return -1;
}

static sly_value
symbol_lookup_props(Sly_State *ss, sly_value sym, u32 *level, sly_value *uplist)
{
	sly_value entry;
	struct compile *cc = ss->cc;
	struct scope *scope = cc->cscope;
	while (scope) {
		entry = dictionary_entry_ref(scope->symtable, sym);
		if (!slot_is_free(entry)) {
			if (level)  {
				*level = scope->level;
			}
			if (uplist) {
				prototype *proto = GET_PTR(scope->proto);
				*uplist = proto->uplist;
			}
			return cdr(entry);
		}
		scope = scope->parent;
	}
	printf("Undefined symbol '");
	sly_display(sym, 1);
	printf("\n");
	sly_raise_exception(ss, EXC_COMPILE, "Error undefined symbol");
	return SLY_NULL;
}

static sly_value
lookup_macro(Sly_State *ss, sly_value sym)
{
	struct scope *scope = ss->cc->cscope;
	sly_value entry;
	while (scope) {
		entry = dictionary_entry_ref(scope->macros, sym);
		if (!slot_is_free(entry)) {
			return cdr(entry);
		}
		scope = scope->parent;
	}
	printf("Undefined macro '");
	sly_display(sym, 1);
	printf("\n");
	sly_raise_exception(ss, EXC_COMPILE, "Error undefined macro");
	return SLY_NULL;
}

static size_t
intern_constant(Sly_State *ss, sly_value value)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	size_t len = vector_len(proto->K);
	for (size_t i = 0; i < len; ++i) {
		if (sly_equal(value, vector_ref(proto->K, i))) {
			return i;
		}
	}
	vector_append(ss, proto->K, value);
	return len;
}

static struct scope *
make_scope(Sly_State *ss)
{
	struct scope *scope = gc_alloc(ss, sizeof(*scope));
	scope->h.type = tt_scope;
	scope->parent = NULL;
	scope->macros = make_dictionary(ss);
	scope->symtable = make_dictionary(ss);
	scope->proto = make_prototype(ss,
								  make_vector(ss, 0, 8),
								  make_vector(ss, 0, 8),
								  make_vector(ss, 0, 8),
								  0, 0, 0, 0);
	return scope;
}

static void
init_symtable(Sly_State *ss, sly_value symtable)
{
	union symbol_properties prop = { .p = { .type = sym_keyword } };
	for (int i = 0; i < KW_COUNT; ++i) {
		sly_value sym = make_symbol(ss, keywords(i), strlen(keywords(i)));
		dictionary_set(ss, symtable, sym, prop.v);
		kw_symbols[i] = sym;
	}
}

int
comp_define(Sly_State *ss, sly_value form, int reg, int is_syntax)
{
	/* TODO: Definitions should only be allowed at the top-level
	 * or the begining of bodies.
	 */
	struct compile *cc = ss->cc;
	sly_value stx = car(form);
	syntax *syn = GET_PTR(stx);
	int line_number = syn->tok.ln;
	sly_value var;
	if (pair_p(stx)) { /* function definition */
		var = syntax_to_datum(car(stx));
		sly_value params = cdr(stx);
		/* build lambda form */
		form = cons(ss,
					cons(ss,
						 make_syntax(ss, (token){0}, make_symbol(ss, "lambda", 6)),
						 cons(ss, params, cdr(form))), SLY_NULL);
	} else {
		var = syntax_to_datum(stx);
		form = cdr(form);
	}
	sly_value symtable = cc->cscope->symtable;
	prototype *proto = GET_PTR(cc->cscope->proto);
	union symbol_properties st_prop = {0};
	if (!symbol_p(var)) {
		sly_raise_exception(ss, EXC_COMPILE, "Error variable name must be a symbol");
	}
	sly_value globals  = cc->globals;
	if (IS_GLOBAL(cc->cscope)) {
		stx = car(form);
		sly_value datum = syntax_p(stx) ? syntax_to_datum(stx) : stx;
		st_prop.p.islocal = 0;
		st_prop.p.type = sym_global;
		st_prop.p.reg = intern_constant(ss, var);
		dictionary_set(ss, symtable, var, st_prop.v);
		if (pair_p(datum) || symbol_p(datum)) {
			dictionary_set(ss, globals, var, SLY_VOID);
			comp_expr(ss, car(form), reg);
			if (is_syntax) {
				STORE_MACRO_CLOSURE();
			}
			if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg + 1, st_prop.p.reg, line_number));
			vector_append(ss, proto->code, iABC(OP_SETUPDICT, 0, reg + 1, reg, line_number));
		} else {
			dictionary_set(ss, globals, var, datum);
		}
		if (!null_p(cdr(form))) {
			sly_raise_exception(ss, EXC_COMPILE, "Compile Error malformed define");
		}
		return reg;
	} else {
		st_prop.p.reg = proto->nvars++;
		st_prop.p.islocal = 1;
		st_prop.p.type = sym_variable;
		dictionary_set(ss, symtable, var, st_prop.v);
		comp_expr(ss, car(form), st_prop.p.reg);
		if (is_syntax) {
			STORE_MACRO_CLOSURE();
		}
		/* end of definition */
		if (!null_p(cdr(form))) {
			sly_raise_exception(ss, EXC_COMPILE, "Compile Error malformed define");
		}
		return proto->nvars;
	}
}

int
comp_if(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	sly_value boolexpr = car(form);
	form = cdr(form);
	sly_value tbranch = car(form);
	form = cdr(form);
	sly_value fbranch = car(form);
	form = cdr(form);
	if (!null_p(form)) {
		sly_raise_exception(ss, EXC_COMPILE, "Compile Error malformed if expression");
	}
	int be_res = comp_expr(ss, boolexpr, reg);
	size_t fjmp = vector_len(proto->code);
	vector_append(ss, proto->code, 0);
	{ /* tbranch */
		int line_number;
		if (syntax_p(tbranch)) {
			syntax *s = GET_PTR(tbranch);
			line_number = s->tok.ln;
		} else {
			line_number = -1;
		}
		int ex_reg = comp_expr(ss, tbranch, reg);
		if (ex_reg != -1 && ex_reg != reg) {
			vector_append(ss, proto->code,
						  iAB(OP_MOVE, reg, ex_reg, line_number));
		}
	}
	size_t jmp = vector_len(proto->code);
	vector_append(ss, proto->code, 0);
	{ /* fbranch */
		int line_number;
		if (syntax_p(fbranch)) {
			syntax *s = GET_PTR(fbranch);
			line_number = s->tok.ln;
		} else {
			line_number = -1;
		}
		int ex_reg = comp_expr(ss, fbranch, reg);
		if (ex_reg != -1 && ex_reg != reg) {
			vector_append(ss, proto->code,
						  iAB(OP_MOVE, reg, ex_reg, line_number));
		}
	}
	size_t end = vector_len(proto->code);
	if (be_res == -1) {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, reg, jmp + 1, -1));
	} else {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, be_res, jmp + 1, -1));
	}
	vector_set(proto->code, jmp,  iAx(OP_JMP, end, -1));
	return reg;
}

static union symbol_properties
resolve_upval(Sly_State *ss,
			  struct scope *scope,
			  sly_value sym,
			  union symbol_properties prop,
			  u32 level)
{
	struct scope *parent = scope->parent;
	union symbol_properties upprop = {0};
	union uplookup upinfo = {0};
	prototype *proto = GET_PTR(scope->proto);
	if (parent->level != level) {
		prop = resolve_upval(ss, parent, sym, prop, parent->level);
	}
	if (prop.p.type == sym_upval) {
		upinfo.u.isup = 1;
	} else if (prop.p.type == sym_variable
			   || prop.p.type == sym_arg) {
		upinfo.u.isup = 0;
	} else {
		sly_raise_exception(ss, EXC_COMPILE, "Compile error");
	}
	upinfo.u.reg = prop.p.reg;
	upprop.p.type = sym_upval;
	upprop.p.reg = vector_len(proto->uplist) + 1; /* + 1 because first slot is reserved for globals */
	vector_append(ss, proto->uplist, upinfo.v);
	dictionary_set(ss, scope->symtable, sym, upprop.v);
	return upprop;
}

int
comp_set(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	form = cdr(form);
	sly_value stx = car(form);
	syntax *syn = GET_PTR(stx);
	int line_number = syn->tok.ln;
	sly_value datum = syntax_to_datum(stx);
	prototype *proto = GET_PTR(cc->cscope->proto);
	if (!symbol_p(datum)) {
		sly_raise_exception(ss, EXC_COMPILE, "Error must be a variable");
	}
	u32 level = 0;
	sly_value uplist = SLY_NULL;
	union symbol_properties st_prop;
	st_prop.v = symbol_lookup_props(ss, datum, &level, &uplist);
	if (cc->cscope->level != level && st_prop.p.type != sym_global) { /* is non local */
		st_prop = resolve_upval(ss, cc->cscope, datum, st_prop, level);
	}
	form = cdr(form);
	reg = comp_expr(ss, car(form), reg);
	if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
	if (st_prop.p.type == sym_variable
		|| st_prop.p.type == sym_arg) {
		if (reg != -1 && st_prop.p.reg != reg) {
			vector_append(ss, proto->code, iAB(OP_MOVE, st_prop.p.reg, reg, line_number));
		}
	} else if (st_prop.p.type == sym_global) {
		st_prop.p.reg = intern_constant(ss, datum);
		if (!IS_GLOBAL(cc->cscope)) {
			st_prop.p.islocal = 1;
			dictionary_set(ss, cc->cscope->symtable, datum, st_prop.v);
		}
		if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
		vector_append(ss, proto->code,
					  iABx(OP_LOADK, reg + 1, st_prop.p.reg, line_number));
		vector_append(ss, proto->code,
					  iABC(OP_SETUPDICT, 0, reg + 1, reg, line_number));
	} else if (st_prop.p.type == sym_upval) {
		vector_append(ss, proto->code,
					  iAB(OP_SETUPVAL, st_prop.p.reg, reg, line_number));
	}
	if (!null_p(cdr(form))) {
		sly_raise_exception(ss, EXC_COMPILE, "Error malformed set! expression");
	}
	return reg;
}

int
comp_atom(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	int line_number = -1;
	if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
	sly_value datum;
	datum = syntax_to_datum(form);
	syntax *syn = GET_PTR(form);
	line_number = syn->tok.ln;
	if (symbol_p(datum)) {
		u32 level = 0;
		sly_value uplist = SLY_NULL;
		union symbol_properties st_prop;
		st_prop.v = symbol_lookup_props(ss, datum, &level, &uplist);
		if (cc->cscope->level != level && st_prop.p.type != sym_global) { /* is non local */
			st_prop = resolve_upval(ss, cc->cscope, datum, st_prop, level);
		}
		switch ((enum symbol_type)st_prop.p.type) {
		case sym_variable:
		case sym_arg: {
			return st_prop.p.reg;
		} break;
		case sym_constant: {
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, st_prop.p.reg, line_number));
		} break;
		case sym_datum: {
			sly_assert(0, "Syntax Datum??");
		} break;
		case sym_keyword: {
			sly_raise_exception(ss, EXC_COMPILE, "Unexpected keyword");
		} break;
		case sym_upval: {
			vector_append(ss, proto->code,
						  iAB(OP_GETUPVAL, reg, st_prop.p.reg, line_number));
			return reg;
		} break;
		case sym_global: {
			st_prop.p.reg = intern_constant(ss, datum);
			if (!IS_GLOBAL(cc->cscope)) {
				st_prop.p.islocal = 1;
				dictionary_set(ss, cc->cscope->symtable, datum, st_prop.v);
			}
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, st_prop.p.reg, line_number));
			vector_append(ss, proto->code, iABC(OP_GETUPDICT, reg, 0, reg, line_number));
		} break;
		}
	} else { /* constant */
		if (datum == SLY_FALSE) {
			vector_append(ss, proto->code, iA(OP_LOADFALSE, reg, line_number));
		} else if (datum == SLY_TRUE) {
			vector_append(ss, proto->code, iA(OP_LOADTRUE, reg, line_number));
		} else if (null_p(datum)) {
			vector_append(ss, proto->code, iA(OP_LOADNULL, reg, line_number));
		} else if (void_p(datum)) {
			vector_append(ss, proto->code, iA(OP_LOADVOID, reg, line_number));
		} else {
			if (int_p(datum)) {
				i64 i = get_int(datum);
				if (i >= INT16_MIN && i <= INT16_MAX) {
					if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
					vector_append(ss, proto->code, iABx(OP_LOADI, reg, i, line_number));
					return -1;
				}
			}
			size_t idx = intern_constant(ss, datum);
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, idx, line_number));
		}
	}
	return -1;
}

int
comp_funcall(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	sly_value head = car(form);
	form = cdr(form);
	token tok;
	tok.ln = -1;
	if (syntax_p(head)) {
		syntax *s = GET_PTR(head);
		tok = s->tok;
		if (symbol_p(s->datum)) {
			u32 level = 0;
			sly_value uplist = SLY_NULL;
			union symbol_properties st_prop = {0};
			st_prop.v = symbol_lookup_props(ss, s->datum, &level, &uplist);
			if (st_prop.p.issyntax) {
				/* call macro */
				sly_value macro = lookup_macro(ss, s->datum);
				form = call_closure_no_eval(ss, macro, cons(ss, form, SLY_NULL));
				return comp_expr(ss, form, reg);
			}
		}
	}
	int start = reg;
	int reg2 = comp_expr(ss, head, reg);
	if (reg2 != -1 && reg2 != reg) {
		vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, tok.ln));
	}
	reg++;
	while (pair_p(form)) {
		head = car(form);
		syntax *s = GET_PTR(head);
		reg2 = comp_expr(ss, head, reg);
		if (reg2 != -1 && reg2 != reg) {
			vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, s->tok.ln));
		}
		reg++;
		form = cdr(form);
	}
	vector_append(ss, proto->code, iAB(OP_CALL, start, reg, tok.ln));
	if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
	return start + 1;
}

int
comp_lambda(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	struct scope *scope = make_scope(ss);
	scope->parent = cc->cscope;
	scope->level = cc->cscope->level + 1;
	cc->cscope = scope;
	int preg = reg;
	prototype *proto = GET_PTR(scope->proto);
	sly_value symtable = scope->symtable;
	sly_value stx, sym, args = car(form);
	form = cdr(form);
	union symbol_properties st_prop = {0};
	st_prop.p.type = sym_arg;
	st_prop.p.islocal = 1;
	proto->nargs = 0;
	proto->nregs = 0;
	while (pair_p(args)) {
		stx = car(args);
		sym = syntax_to_datum(stx);
		if (!symbol_p(sym)) {
			sly_raise_exception(ss, EXC_COMPILE, "Compile error function parameter must be a symbol");
		}
		proto->nargs++;
		st_prop.p.reg = proto->nregs;
		proto->nregs++;
		dictionary_set(ss, symtable, sym, st_prop.v);
		args = cdr(args);
	}
	if (!null_p(args)) {
		sym = syntax_to_datum(args);
		if (symbol_p(sym)) {
			proto->has_varg = 1;
			st_prop.p.reg =	proto->nregs;
			proto->nregs++;
			dictionary_set(ss, symtable, sym, st_prop.v);
		} else {
			sly_raise_exception(ss, EXC_COMPILE, "Compile error function parameter must be a symbol");
		}
	}
	proto->nvars = proto->nregs;
	while (!null_p(form)) {
		reg = comp_expr(ss, car(form), proto->nvars);
		form = cdr(form);
	}
	vector_append(ss, proto->code, iA(OP_RETURN, reg, -1));
	cc->cscope = cc->cscope->parent;
	reg = preg;
	prototype *cproto = GET_PTR(cc->cscope->proto);
	size_t i = vector_len(cproto->K);
	vector_append(ss, cproto->K, scope->proto);
	if ((size_t)reg >= cproto->nregs) cproto->nregs = reg + 1;
	vector_append(ss, cproto->code, iABx(OP_CLOSURE, reg, i, -1));
	return reg;
}

sly_value
strip_syntax(Sly_State *ss, sly_value form)
{
	if (pair_p(form)) {
		return cons(ss, strip_syntax(ss, car(form)),
					    strip_syntax(ss, cdr(form)));
	}
	if (syntax_p(form)) {
		return syntax_to_datum(form);
	}
	return form;
}

int
comp_expr(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	if (!pair_p(form)) {
		return comp_atom(ss, form, reg);
	}
	sly_value stx, datum;
	stx = car(form);
	if (pair_p(stx)) {
		return comp_funcall(ss, form, reg);
	}
	if (!syntax_p(stx)) {
		sly_display(stx, 1);
		printf("\n");
		sly_raise_exception(ss, EXC_COMPILE, "expected syntax object");
	}
	datum = syntax_to_datum(stx);
	int kw;
	if (symbol_p(datum) && (kw = is_keyword(datum)) != -1) {
		switch ((enum kw)kw) {
		case kw_define: {
			form = cdr(form);
			reg = comp_define(ss, form, reg, 0);
		} break;
		case kw_lambda: {
			form = cdr(form);
			reg = comp_lambda(ss, form, reg);
		} break;
		case kw_quote: {
			syntax *s = GET_PTR(stx);
			form = cdr(form);
			if (!null_p(cdr(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed quote");
			}
			form = car(form);
			form = strip_syntax(ss, form);
			prototype *proto = GET_PTR(ss->cc->cscope->proto);
			if (null_p(form)) {
				vector_append(ss, proto->code, iA(OP_LOADNULL, reg, s->tok.ln));
			} else if (form == SLY_TRUE) {
				vector_append(ss, proto->code, iA(OP_LOADTRUE, reg, s->tok.ln));
			} else if (form == SLY_FALSE) {
				vector_append(ss, proto->code, iA(OP_LOADFALSE, reg, s->tok.ln));
			} else {
				int kreg = intern_constant(ss, form);
				vector_append(ss, proto->code, iABx(OP_LOADK, reg, kreg, s->tok.ln));
			}
		} break;
		case kw_syntax_quote: {
			syntax *s = GET_PTR(stx);
			form = cdr(form);
			if (!null_p(cdr(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed quote");
			}
			form = car(form);
			prototype *proto = GET_PTR(ss->cc->cscope->proto);
			int kreg = intern_constant(ss, form);
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, kreg, s->tok.ln));
		} break;
		case kw_begin: {
			form = cdr(form);
			prototype *proto = GET_PTR(ss->cc->cscope->proto);
			while (!null_p(form)) {
				comp_expr(ss, car(form), proto->nvars);
				form = cdr(form);
			}
		} break;
		case kw_if: {
			form = cdr(form);
			reg = comp_if(ss, form, reg);
		} break;
		case kw_set: {
			comp_set(ss, form, reg);
		} break;
		case kw_define_syntax: {
			form = cdr(form);
			reg = comp_define(ss, form, reg, 1);
		} break;
		case kw_call_with_continuation: /* fallthrough intended */
		case kw_call_cc: {
			prototype *proto = GET_PTR(cc->cscope->proto);
			syntax *s = GET_PTR(stx);
			form = cdr(form);
			int reg2 = comp_expr(ss, car(form), reg);
			if (reg2 == -1) {
				vector_append(ss, proto->code, iA(OP_CALLWCC, reg, s->tok.ln));
			} else {
				vector_append(ss, proto->code, iA(OP_CALLWCC, reg2, s->tok.ln));
			}
			if (!null_p(cdr(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed display expression");
			}
		} break;
		case kw_include: { /* TODO: Hasn't been tested */
			char *src;
			char path[255] = {0};
			form = cdr(form);
			byte_vector *bv = GET_PTR(syntax_to_datum(car(form)));
			memcpy(path, bv->elems, bv->len);
			sly_value ast = parse_file(ss, path, &src);
			FREE(src);
			comp_expr(ss, ast, reg);
		} break;
		case KW_COUNT: {
			sly_raise_exception(ss, EXC_COMPILE, "(KW_COUNT) Not a real keyword");
		} break;
		}
	} else {
		comp_funcall(ss, form, reg);
	}
	return reg;
}

sly_value
cadd(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 0);
	while (!null_p(vargs)) {
		total = sly_add(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
csub(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 0);
	if (null_p(vargs)) {
		return total;
	}
	sly_value head = car(vargs);
	vargs = cdr(vargs);
	if (null_p(vargs)) {
		return sly_sub(ss, total, head);
	}
	while (!null_p(vargs)) {
		total = sly_add(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return sly_sub(ss, head, total);
}

sly_value
cmul(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 1);
	while (!null_p(vargs)) {
		total = sly_mul(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cdiv(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 1);
	if (null_p(vargs)) {
		sly_raise_exception(ss, EXC_GENERIC, "Divide by zero");
	}
	sly_value head = car(vargs);
	vargs = cdr(vargs);
	if (null_p(vargs)) {
		return sly_sub(ss, total, head);
	}
	while (!null_p(vargs)) {
		total = sly_mul(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return sly_div(ss, head, total);
}

sly_value
cmod(Sly_State *ss, sly_value args)
{
	sly_value total = sly_mod(ss, vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_mod(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cnum_eq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_lt(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_lt(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_gt(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_gt(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_leq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_lt(vector_ref(args, 0), vector_ref(args, 1))
				   || sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_geq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_gt(vector_ref(args, 0), vector_ref(args, 1))
				   || sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnot(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	if (vector_ref(args, 0) == SLY_FALSE) {
		return SLY_TRUE;
	} else {
		return SLY_FALSE;
	}
}

sly_value
cnull_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(null_p(vector_ref(args, 0)));
}

sly_value
cpair_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(pair_p(vector_ref(args, 0)));
}

sly_value
cequal_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v1 = vector_ref(args, 0);
	sly_value v2 = vector_ref(args, 1);
	return ctobool(sly_equal(v1, v2));
}

sly_value
cnum_noteq(Sly_State *ss, sly_value args)
{
	vector_set(args, 0, cnum_eq(ss, args));
	return cnot(ss, args);
}

sly_value
ccons(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return cons(ss, vector_ref(args, 0), vector_ref(args, 1));
}

sly_value
ccar(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return car(vector_ref(args, 0));
}

sly_value
ccdr(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return cdr(vector_ref(args, 0));
}

sly_value
cset_car(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v = vector_ref(args, 1);
	set_car(vector_ref(args, 0), v);
	return v;
}

sly_value
cset_cdr(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v = vector_ref(args, 1);
	set_cdr(vector_ref(args, 0), v);
	return v;
}

sly_value
cdisplay(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_display(vector_ref(args, 0), 0);
	return SLY_VOID;
}

sly_value
clist(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return vector_ref(args, 0);
}

sly_value
cgensym(Sly_State *ss, sly_value args)
{
	UNUSED(args);
	return gensym(ss);
}

sly_value
cmake_syntax(Sly_State *ss, sly_value args)
{
	return make_syntax(ss, (token){0}, vector_ref(args, 0));
}

sly_value
cmake_vector(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	sly_value vec = make_vector(ss, 0, 12);
	while (!null_p(list)) {
		vector_append(ss, vec, car(list));
		list = cdr(list);
	}
	return vec;
}

sly_value
cvector_ref(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value vec = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	return vector_ref(vec, get_int(idx));
}

sly_value
cvector_set(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value vec = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	sly_value val = vector_ref(args, 2);
	vector_set(vec, get_int(idx), val);
	return val;
}

sly_value
cvector_length(Sly_State *ss, sly_value args)
{
	sly_value vec = vector_ref(args, 0);
	return make_int(ss, vector_len(vec));
}

void
init_builtins(Sly_State *ss)
{
	struct compile *cc = ss->cc;
	sly_value symtable = cc->cscope->symtable;
	union symbol_properties st_prop = {0};
	sly_value sym;
	st_prop.p.islocal = 0;
	st_prop.p.type = sym_global;
	cc->globals = make_dictionary(ss);
	ADD_BUILTIN("+", cadd, 0, 1);
	ADD_BUILTIN("-", csub, 0, 1);
	ADD_BUILTIN("*", cmul, 0, 1);
	ADD_BUILTIN("/", cdiv, 0, 1);
	ADD_BUILTIN("%", cmod, 2, 1);
	ADD_BUILTIN("=", cnum_eq, 2, 0);
	ADD_BUILTIN("<", cnum_lt, 2, 0);
	ADD_BUILTIN(">", cnum_gt, 2, 0);
	ADD_BUILTIN("<=", cnum_leq, 2, 0);
	ADD_BUILTIN(">=", cnum_geq, 2, 0);
	ADD_BUILTIN("/=", cnum_noteq, 2, 0);
	ADD_BUILTIN("not", cnot, 1, 0);
	ADD_BUILTIN("null?", cnull_p, 1, 0);
	ADD_BUILTIN("pair?", cpair_p, 1, 0);
	ADD_BUILTIN("equal?", cequal_p, 2, 0);
	ADD_BUILTIN("cons", ccons, 2, 0);
	ADD_BUILTIN("car", ccar, 1, 0);
	ADD_BUILTIN("cdr", ccdr, 1, 0);
	ADD_BUILTIN("set-car!", cset_car, 2, 0);
	ADD_BUILTIN("set-cdr!", cset_cdr, 2, 0);
	ADD_BUILTIN("display", cdisplay, 1, 0);
	ADD_BUILTIN("list", clist, 0, 1);
	ADD_BUILTIN("gensym", cgensym, 0, 0);
	ADD_BUILTIN("make-syntax", cmake_syntax, 1, 0);
	ADD_BUILTIN("make-vector", cmake_vector, 0, 1);
	ADD_BUILTIN("vector-ref", cvector_ref, 2, 0);
	ADD_BUILTIN("vector-set", cvector_set, 3, 0);
	ADD_BUILTIN("vector-length", cvector_length, 1, 0);
}

int
sly_compile(Sly_State *ss, sly_value ast)
{
	HANDLE_EXCEPTION(ss, {
			fprintf(stderr, "Caught exception in %s ", ss->file_path);
			fprintf(stderr, "during compilation:\n");
			fprintf(stderr, "%s\n", ss->excpt_msg);
			return ss->excpt;
		});
	prototype *proto;
	ss->cc = MALLOC(sizeof(*ss->cc));
	if (ss->cc == NULL) {
		sly_raise_exception(ss, EXC_ALLOC, "(sly_compile) malloc failed; returned NULL");
	}
	assert(ss->cc != NULL);
	struct compile *cc = ss->cc;
	cc->cscope = make_scope(ss);
	init_symtable(ss, cc->cscope->symtable);
	cc->cscope->level = 0; /* top level */
	init_builtins(ss);
	proto = GET_PTR(cc->cscope->proto);
	int r = comp_expr(ss, ast, 0);
	vector_append(ss, proto->code, iA(OP_RETURN, r, -1));
	END_HANDLE_EXCEPTION(ss);
	return 0;
}
