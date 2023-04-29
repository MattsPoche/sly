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
	kw_load,
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

#define IS_GLOBAL(scope) ((scope)->parent == NULL)
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

#define CAR(val) (syntax_p(val) ? car(syntax_to_datum(val)) : car(val))
#define CDR(val) (syntax_p(val) ? cdr(syntax_to_datum(val)) : cdr(val))
#define syntax_cons(car, cdr) make_syntax(ss, (token){0}, cons(ss, car, cdr))

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
	case kw_load: return "load";
	case KW_COUNT: break;
	}
	sly_assert(0, "Error, No such keyword");
	return NULL;
}

static sly_value kw_symbols[KW_COUNT];

int comp_expr(Sly_State *ss, sly_value form, int reg);
int comp_atom(Sly_State *ss, sly_value form, int reg);
static void init_symtable(Sly_State *ss, sly_value symtable);
static size_t intern_constant(Sly_State *ss, sly_value value);
static void setup_frame(Sly_State *ss);

static void
define_global_var(Sly_State *ss, sly_value var, sly_value val)
{
	sly_value symtable = ss->cc->cscope->symtable;
	sly_value globals = ss->cc->globals;
	union symbol_properties st_prop = {0};
	st_prop.p.type = sym_global;
	st_prop.p.reg = intern_constant(ss, var);
	dictionary_set(ss, symtable, var, st_prop.v);
	dictionary_set(ss, globals, var, val);
}

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
	sly_value stx = CAR(form);
	syntax *syn = GET_PTR(stx);
	int line_number = syn->tok.ln;
	sly_value var;
	if (syntax_pair_p(stx)) { /* function definition */
		var = syntax_to_datum(CAR(stx));
		sly_value params = CDR(stx);
		/* build lambda form */
		form = syntax_cons(syntax_cons(make_syntax(ss, (token){0}, make_symbol(ss, "lambda", 6)),
									   cons(ss, params, CDR(form))), SLY_NULL);
	} else {
		var = syntax_to_datum(stx);
		form = CDR(form);
	}
	sly_value symtable = cc->cscope->symtable;
	prototype *proto = GET_PTR(cc->cscope->proto);
	union symbol_properties st_prop = {0};
	if (!symbol_p(var)) {
		sly_display(var, 1);
		printf("\n");
		sly_raise_exception(ss, EXC_COMPILE, "Error variable name must be a symbol");
	}
	sly_value globals  = cc->globals;
	if (IS_GLOBAL(cc->cscope)) {
		stx = CAR(form);
		sly_value datum = syntax_to_datum(stx);
		st_prop.p.islocal = 0;
		st_prop.p.type = sym_global;
		st_prop.p.reg = intern_constant(ss, var);
		dictionary_set(ss, symtable, var, st_prop.v);
		if (pair_p(datum) || symbol_p(datum)) {
			dictionary_set(ss, globals, var, SLY_VOID);
			comp_expr(ss, CAR(form), reg);
			if (is_syntax) {
				STORE_MACRO_CLOSURE();
			}
			if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg + 1, st_prop.p.reg, line_number));
			vector_append(ss, proto->code, iABC(OP_SETUPDICT, 0, reg + 1, reg, line_number));
		} else {
			dictionary_set(ss, globals, var, datum);
		}
		if (!null_p(CDR(form))) {
			sly_raise_exception(ss, EXC_COMPILE, "Compile Error malformed define");
		}
		return reg;
	} else {
		st_prop.p.reg = proto->nvars++;
		st_prop.p.islocal = 1;
		st_prop.p.type = sym_variable;
		dictionary_set(ss, symtable, var, st_prop.v);
		comp_expr(ss, CAR(form), st_prop.p.reg);
		if (is_syntax) {
			STORE_MACRO_CLOSURE();
		}
		/* end of definition */
		if (!null_p(CDR(form))) {
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
	syntax *stx = GET_PTR(form);
	form = CDR(form);
	sly_value boolexpr = CAR(form);
	form = CDR(form);
	sly_value tbranch = CAR(form);
	form = CDR(form);
	sly_value fbranch = CAR(form);
	form = CDR(form);
	if (!null_p(form)) {
		sly_raise_exception(ss, EXC_COMPILE, "Compile Error malformed if expression");
	}
	if (stx->context & ctx_tail_pos) {
		syntax *s = GET_PTR(tbranch);
		s->context |= ctx_tail_pos;
		s = GET_PTR(fbranch);
		s->context |= ctx_tail_pos;
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
	form = CDR(form);
	sly_value stx = CAR(form);
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
	form = CDR(form);
	reg = comp_expr(ss, CAR(form), reg);
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
	if (!null_p(CDR(form))) {
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
	if (!syntax_p(form)) {
		sly_display(form, 1);
		printf("\n");
	}
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
	syntax *stx = GET_PTR(form);
	sly_value head = CAR(form);
	form = CDR(form);
	token tok;
	tok.ln = -1;
	if (identifier_p(head)) {
		syntax *s = GET_PTR(head);
		tok = s->tok;
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
	int start = reg;
	int reg2 = comp_expr(ss, head, reg);
	if (reg2 != -1 && reg2 != reg) {
		vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, tok.ln));
	}
	reg++;
	while (!null_p(form)) {
		head = CAR(form);
		syntax *s = GET_PTR(head);
		reg2 = comp_expr(ss, head, reg);
		if (reg2 != -1 && reg2 != reg) {
			vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, s->tok.ln));
		}
		reg++;
		form = CDR(form);
	}
	if (stx->context & ctx_tail_pos) {
		vector_append(ss, proto->code, iAB(OP_TAILCALL, start, reg, tok.ln));
	} else {
		vector_append(ss, proto->code, iAB(OP_CALL, start, reg, tok.ln));
	}
	if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
	return start;
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
	sly_value stx, sym, args = CAR(form);
	form = CDR(form);
	union symbol_properties st_prop = {0};
	st_prop.p.type = sym_arg;
	st_prop.p.islocal = 1;
	proto->nargs = 0;
	proto->nregs = 0;
	while (pair_p(args) || syntax_pair_p(args)) {
		stx = CAR(args);
		sym = syntax_to_datum(stx);
		if (!symbol_p(sym)) {
			sly_display(sym, 1);
			printf("\n");
			sly_raise_exception(ss, EXC_COMPILE, "Compile error function parameter must be a symbol");
		}
		proto->nargs++;
		st_prop.p.reg = proto->nregs;
		proto->nregs++;
		dictionary_set(ss, symtable, sym, st_prop.v);
		args = CDR(args);
	}
	if (!null_p(args)) {
		sym = syntax_to_datum(args);
		if (symbol_p(sym)) {
			proto->has_varg = 1;
			st_prop.p.reg =	proto->nregs;
			proto->nregs++;
			dictionary_set(ss, symtable, sym, st_prop.v);
		} else {
			sly_display(sym, 1);
			printf("\n");
			sly_raise_exception(ss, EXC_COMPILE, "Compile error function parameter must be a symbol");
		}
	}
	proto->nvars = proto->nregs;
	{
		while (!null_p(CDR(form))) {
			comp_expr(ss, CAR(form), proto->nvars);
			form = CDR(form);
		}
		syntax *stx = GET_PTR(CAR(form));
		stx->context |= ctx_tail_pos;
		reg = comp_expr(ss, CAR(form), proto->nvars);
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
	if (syntax_pair_p(form)) {
		return strip_syntax(ss, syntax_to_datum(form));
	}
	if (pair_p(form)) {
		return cons(ss, strip_syntax(ss, CAR(form)),
					    strip_syntax(ss, CDR(form)));
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
	if (!syntax_pair_p(form)) {
		return comp_atom(ss, form, reg);
	}
	sly_value stx, datum;
	stx = CAR(form);
	if (syntax_pair_p(stx)) {
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
			form = CDR(form);
			reg = comp_define(ss, form, reg, 0);
		} break;
		case kw_lambda: {
			form = CDR(form);
			reg = comp_lambda(ss, form, reg);
		} break;
		case kw_quote: {
			syntax *s = GET_PTR(stx);
			form = CDR(form);
			if (!null_p(CDR(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed quote");
			}
			form = CAR(form);
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
			form = CDR(form);
			if (!null_p(CDR(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed quote");
			}
			form = CAR(form);
			prototype *proto = GET_PTR(ss->cc->cscope->proto);
			int kreg = intern_constant(ss, form);
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, kreg, s->tok.ln));
		} break;
		case kw_begin: {
			syntax *s = GET_PTR(form);
			form = CDR(form);
			prototype *proto = GET_PTR(ss->cc->cscope->proto);
			while (!null_p(CDR(form))) {
				comp_expr(ss, CAR(form), proto->nvars);
				form = CDR(form);
			}
			if (s->context & ctx_tail_pos) {
				s = GET_PTR(CAR(form));
				s->context |= ctx_tail_pos;
			}
			comp_expr(ss, CAR(form), proto->nvars);
		} break;
		case kw_if: {
			reg = comp_if(ss, form, reg);
		} break;
		case kw_set: {
			comp_set(ss, form, reg);
		} break;
		case kw_define_syntax: {
			form = CDR(form);
			reg = comp_define(ss, form, reg, 1);
		} break;
		case kw_call_with_continuation: /* fallthrough intended */
		case kw_call_cc: {
			prototype *proto = GET_PTR(cc->cscope->proto);
			syntax *s = GET_PTR(stx);
			form = CDR(form);
			int reg2 = comp_expr(ss, CAR(form), reg);
			if (reg2 == -1) {
				vector_append(ss, proto->code, iA(OP_CALLWCC, reg, s->tok.ln));
			} else {
				vector_append(ss, proto->code, iA(OP_CALLWCC, reg2, s->tok.ln));
			}
			if (!null_p(CDR(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed display expression");
			}
		} break;
		case kw_load: {
			char *src;
			char path[255] = {0};
			sly_assert(ss->cc->cscope->level == 0,
					   "Error: load form is only allowed in a top-level context");
			form = CDR(form);
			byte_vector *bv = GET_PTR(syntax_to_datum(CAR(form)));
			memcpy(path, bv->elems, bv->len);
			sly_value ast = parse_file(ss, path, &src);
			struct scope *pscope = ss->cc->cscope;
			ss->cc->cscope = make_scope(ss);
			ss->cc->cscope->symtable = pscope->symtable;
			init_symtable(ss, ss->cc->cscope->symtable);
			ss->cc->cscope->level = 0; /* top level */
			sly_compile(ss, ast);
			setup_frame(ss);
			vm_run(ss, 0);
			ss->cc->cscope = pscope;
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

#include "builtins.h"

void
sly_init_state(Sly_State *ss)
{
	*ss = (Sly_State){0};
	ss->interned = make_dictionary(ss);
	ss->cc = MALLOC(sizeof(*ss->cc));
	ss->cc->globals = make_dictionary(ss);
	ss->cc->cscope = make_scope(ss);
	ss->interned = make_dictionary(ss);
	init_symtable(ss, ss->cc->cscope->symtable);
	ss->cc->cscope->level = 0; /* top level */
	init_builtins(ss);
	define_global_var(ss, make_symbol(ss, "__file__", 8), SLY_FALSE);
	define_global_var(ss, make_symbol(ss, "__name__", 8), SLY_FALSE);
}

static void
setup_frame(Sly_State *ss)
{
	ss->proto = ss->cc->cscope->proto;
	prototype *proto = GET_PTR(ss->proto);
	stack_frame *frame = make_stack(ss, proto->nregs);
	frame->U = make_vector(ss, 12, 12);
	for (size_t i = 0; i < 12; ++i) {
		vector_set(frame->U, i, SLY_VOID);
	}
	vector_set(frame->U, 0, make_open_upvalue(ss, &ss->cc->globals));
	frame->K = proto->K;
	frame->clos = SLY_NULL;
	frame->code = proto->code;
	frame->pc = proto->entry;
	frame->level = 0;
	ss->frame = frame;
}

void
sly_free_state(Sly_State *ss)
{
	gc_free_all(ss);
	if (ss->cc) {
		FREE(ss->cc);
		ss->cc = NULL;
	}
	if (ss->source_code) {
		FREE(ss->source_code);
		ss->source_code = NULL;
	}
}

void
sly_do_file(char *file_path, int debug_info)
{
	Sly_State ss = {0};
	sly_value globals;
	sly_init_state(&ss);
	sly_compile(&ss, parse_file(&ss, file_path, &ss.source_code));
	setup_frame(&ss);
	gc_collect(&ss);
	globals = ss.cc->globals;
	dictionary_set(&ss, globals,
				   make_symbol(&ss, "__file__", 8),
				   make_string(&ss, file_path, strlen(file_path)));
	dictionary_set(&ss, globals,
				   make_symbol(&ss, "__name__", 8),
				   make_string(&ss, file_path, strlen(file_path)));
	if (debug_info) dis_all(ss.frame, 1);
	vm_run(&ss, 1);
	sly_free_state(&ss);
	if (debug_info) {
		printf("** Allocations: %d **\n", allocations);
		printf("** Net allocations: %d **\n", net_allocations);
		printf("** Total bytes allocated: %zu **\n", bytes_allocated);
		printf("** GC Total Collections: %d **\n", ss.gc.collections);
	}
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
	prototype *proto = GET_PTR(ss->cc->cscope->proto);
	int r = comp_expr(ss, ast, 0);
	vector_append(ss, proto->code, iA(OP_RETURN, r, -1));
	END_HANDLE_EXCEPTION(ss);
	return 0;
}
