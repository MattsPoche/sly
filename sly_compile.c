#include <assert.h>
#include "sly_types.h"
#include "parser.h"
#define OPCODES_INCLUDE_INLINE 1
#include "opcodes.h"
#include "sly_compile.h"
#include "eval.h"
#include "sly_vm.h"
#include "sly_alloc.h"

/* TODO: Associate syntactic info with opcodes (semi-done)
 */

/* TODO: procedures cannot refer to variables defined
 * after they are.
 */

/* TODO: macros are unable to call functions defined in the same file.
 */

/* TODO: properly implement vector literals
 */

/* NOTE: try storing globals as upvalues for faster global access.
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
		u8 issyntax;
		u8 isundefined;
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
		sly_value _clos = make_closure(ss, p);				\
		closure *clos = GET_PTR(_clos);						\
		vector_set(clos->upvals, 0,							\
				   make_closed_upvalue(ss, globals));		\
		dictionary_set(ss, cc->cscope->macros, var, _clos);	\
	} while (0)
#define EMPTY_SYNTAX() make_syntax(ss, (token){0}, SLY_NULL)

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
static int comp_atom(Sly_State *ss, sly_value form, int reg);
static void init_builtins(Sly_State *ss);
static void init_symtable(Sly_State *ss, sly_value symtable);
static size_t intern_constant(Sly_State *ss, sly_value value);
static void setup_frame(Sly_State *ss);
static void load_file(Sly_State *ss, sly_value form);
static void forward_scan_block(Sly_State *ss, sly_value form);
static sly_value expand(Sly_State *ss, sly_value form);

static void
define_global_var(Sly_State *ss, sly_value var, sly_value val)
{
	sly_value symtable = ss->cc->cscope->symtable;
	sly_value globals = ss->cc->globals;
	union symbol_properties st_prop = {0};
	st_prop.p.type = sym_global;
	dictionary_set(ss, symtable, var, st_prop.v);
	dictionary_set(ss, globals, var, val);
}

static int
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
	return SLY_VOID;
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
	return SLY_VOID;
}

static size_t
intern_in_vector(Sly_State *ss, sly_value vec, sly_value value)
{
	size_t len = vector_len(vec);
	for (size_t i = 0; i < len; ++i) {
		if (sly_equal(value, vector_ref(vec, i))) {
			return i;
		}
	}
	vector_append(ss, vec, value);
	return len;
}

static size_t
intern_constant(Sly_State *ss, sly_value value)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	return intern_in_vector(ss, proto->K, value);
}

static size_t
intern_syntax(Sly_State *ss, sly_value value)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	return intern_in_vector(ss, proto->syntax_info, value);
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

static void
apply_context(sly_value form, int ctx)
{
	if (syntax_p(form)) {
		syntax *stx = GET_PTR(form);
		stx->context = FLAG_ON(stx->context, ctx);
	}
	if (pair_p(form) || syntax_pair_p(form)) {
		apply_context(CAR(form), ctx);
		apply_context(CDR(form), ctx);
	}
}

static int
comp_define(Sly_State *ss, sly_value form, int reg, int is_syntax)
{
	struct compile *cc = ss->cc;
	sly_value globals  = cc->globals;
	sly_value stx = CAR(form);
	sly_value symtable = cc->cscope->symtable;
	prototype *proto = GET_PTR(cc->cscope->proto);
	sly_value var;
	if (syntax_pair_p(stx)) { /* function definition */
		var = syntax_to_datum(CAR(stx));
		sly_value params = CDR(stx);
		stx = CAR(stx); /* name */
		/* build lambda form */
		form = syntax_cons(syntax_cons(make_syntax(ss, (token){0}, make_symbol(ss, "lambda", 6)),
									   cons(ss, params, CDR(form))), SLY_NULL);
		if (is_syntax) apply_context(form, ctx_macro_body);
	} else {
		var = syntax_to_datum(stx);
		form = CDR(form);
	}
	union symbol_properties st_prop = {0};
	st_prop.v = symbol_lookup_props(ss, var, NULL, NULL);
	if (void_p(st_prop.v)) {
		sly_raise_exception(ss, EXC_COMPILE,
							"Error definition form ouside definition context");
	}
	if (st_prop.p.isundefined == 0) {
		sly_raise_exception(ss, EXC_COMPILE,
							"Error re-definition of local variable");
	}
	if (!symbol_p(var)) {
		sly_display(var, 1);
		printf("\n");
		sly_raise_exception(ss, EXC_COMPILE, "Error variable name must be a symbol");
	}
	/* define variable */
	st_prop.p.isundefined = 0; // no longer undefined
	dictionary_set(ss, symtable, var, st_prop.v);
	if (st_prop.p.type == sym_global) {
		sly_value datum = syntax_to_datum(stx);
		if (pair_p(datum) || symbol_p(datum)) {
			dictionary_set(ss, globals, var, SLY_VOID);
			comp_expr(ss, CAR(form), reg);
			if (is_syntax) {
				STORE_MACRO_CLOSURE();
			}
			if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
			int t = intern_syntax(ss, stx);
			vector_append(ss, proto->code, iABx(OP_LOADK, reg + 1, st_prop.p.reg, t));
			vector_append(ss, proto->code, iABC(OP_SETUPDICT, 0, reg + 1, reg, t));
		} else {
			dictionary_set(ss, globals, var, datum);
		}
		if (!null_p(CDR(form))) {
			sly_raise_exception(ss, EXC_COMPILE, "Compile Error malformed define");
		}
		return reg;
	} else {
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

static int
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
	if (tbranch && stx->context & ctx_tail_pos) {
		syntax *s = GET_PTR(tbranch);
		s->context = FLAG_ON(s->context, ctx_tail_pos);
	}
	if (fbranch && stx->context & ctx_tail_pos) {
		syntax *s = GET_PTR(fbranch);
		s->context = FLAG_ON(s->context, ctx_tail_pos);
	}
	int be_res = comp_expr(ss, boolexpr, reg);
	size_t fjmp = vector_len(proto->code);
	u32 t = -1;
	vector_append(ss, proto->code, 0);
	{ /* tbranch */
		if (syntax_p(tbranch)) {
			t = intern_syntax(ss, tbranch);
		}
		int ex_reg = comp_expr(ss, tbranch, reg);
		if (ex_reg != -1 && ex_reg != reg) {
			vector_append(ss, proto->code,
						  iAB(OP_MOVE, reg, ex_reg, t));
		}
	}
	size_t jmp = vector_len(proto->code);
	vector_append(ss, proto->code, 0);
	{ /* fbranch */
		t = -1;
		if (syntax_p(fbranch)) {
			t = intern_syntax(ss, fbranch);
		}
		int ex_reg = comp_expr(ss, fbranch, reg);
		if (ex_reg != -1 && ex_reg != reg) {
			vector_append(ss, proto->code,
						  iAB(OP_MOVE, reg, ex_reg, t));
		}
	}
	size_t end = vector_len(proto->code);
	if (be_res == -1) {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, reg, jmp + 1, t));
	} else {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, be_res, jmp + 1, t));
	}
	vector_set(proto->code, jmp, iAx(OP_JMP, end, -1));
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
		prop = resolve_upval(ss, parent, sym, prop, level);
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

static int
comp_set(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	form = CDR(form);
	sly_value stx = CAR(form);
	sly_value datum = syntax_to_datum(stx);
	prototype *proto = GET_PTR(cc->cscope->proto);
	u32 src_info = intern_syntax(ss, stx);
	if (!symbol_p(datum)) {
		sly_raise_exception(ss, EXC_COMPILE, "Error must be a variable");
	}
	u32 level = 0;
	sly_value uplist = SLY_NULL;
	union symbol_properties st_prop;
	st_prop.v = symbol_lookup_props(ss, datum, &level, &uplist);
	if (void_p(st_prop.v)) {
		printf("Undefined symbol '");
		sly_display(datum, 1);
		printf("\n");
		sly_raise_exception(ss, EXC_COMPILE, "Error undefined symbol (418)");
	}
	if (cc->cscope->level == level && st_prop.p.isundefined) {
		sly_raise_exception(ss, EXC_COMPILE,
							"Error symbol assigned before it is defined (418)");
	}
	if (cc->cscope->level != level && st_prop.p.type != sym_global) { /* is non local */
		st_prop = resolve_upval(ss, cc->cscope, datum, st_prop, level);
	}
	form = CDR(form);
	int preg = reg;
	reg = comp_expr(ss, CAR(form), reg);
	if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
	if (st_prop.p.type == sym_variable
		|| st_prop.p.type == sym_arg) {
		if (reg != -1 && st_prop.p.reg != reg) {
			vector_append(ss, proto->code, iAB(OP_MOVE, st_prop.p.reg, reg, src_info));
		}
	} else if (st_prop.p.type == sym_global) {
		st_prop.p.reg = intern_constant(ss, datum);
		if (!IS_GLOBAL(cc->cscope)) {
			dictionary_set(ss, cc->cscope->symtable, datum, st_prop.v);
		}
		if (reg == -1) reg = preg;
		if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
		vector_append(ss, proto->code,
					  iABx(OP_LOADK, reg + 1, st_prop.p.reg, src_info));
		vector_append(ss, proto->code,
					  iABC(OP_SETUPDICT, 0, reg + 1, reg, src_info));
	} else if (st_prop.p.type == sym_upval) {
		vector_append(ss, proto->code,
					  iAB(OP_SETUPVAL, st_prop.p.reg, reg, src_info));
	}
	if (!null_p(CDR(form))) {
		sly_raise_exception(ss, EXC_COMPILE, "Error malformed set! expression");
	}
	return reg;
}

static int
comp_atom(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	int src_info = -1;
	if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
	sly_value datum;
	if (null_p(form) || void_p(form)) {
		datum = form;
	} else {
		datum = syntax_to_datum(form);
		src_info = intern_syntax(ss, form);
	}
	if (symbol_p(datum)) {
		u32 level = 0;
		sly_value uplist = SLY_NULL;
		union symbol_properties st_prop;
		st_prop.v = symbol_lookup_props(ss, datum, &level, &uplist);
		if (void_p(st_prop.v)) {
			printf("Undefined symbol '");
			sly_display(datum, 1);
			printf("\n");
			syntax *s = GET_PTR(form);
			printf("%s:%d:%d\n", ss->file_path, s->tok.ln, s->tok.cn);
			sly_raise_exception(ss, EXC_COMPILE, "Error undefined symbol (479)");
		}
/* TODO: Fix this
		if (cc->cscope->level == level && st_prop.p.isundefined) {
			sly_display(datum, 1);
			printf("\n");
			sly_raise_exception(ss, EXC_COMPILE, "Error undefined symbol (493)");
		}
*/
		if (cc->cscope->level != level && st_prop.p.type != sym_global) { /* is non local */
			st_prop = resolve_upval(ss, cc->cscope, datum, st_prop, level);
		}
		switch ((enum symbol_type)st_prop.p.type) {
		case sym_variable:
		case sym_arg: {
			return st_prop.p.reg;
		} break;
		case sym_constant: {
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, st_prop.p.reg, src_info));
		} break;
		case sym_datum: {
			sly_assert(0, "Syntax Datum??");
		} break;
		case sym_keyword: {
			sly_raise_exception(ss, EXC_COMPILE, "Unexpected keyword");
		} break;
		case sym_upval: {
			vector_append(ss, proto->code,
						  iAB(OP_GETUPVAL, reg, st_prop.p.reg, src_info));
			return reg;
		} break;
		case sym_global: {
			st_prop.p.reg = intern_constant(ss, datum);
			if (!IS_GLOBAL(cc->cscope)) {
				dictionary_set(ss, cc->cscope->symtable, datum, st_prop.v);
			}
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, st_prop.p.reg, src_info));
			vector_append(ss, proto->code, iABC(OP_GETUPDICT, reg, 0, reg, src_info));
		} break;
		}
	} else { /* constant */
		if (datum == SLY_FALSE) {
			vector_append(ss, proto->code, iA(OP_LOADFALSE, reg, src_info));
		} else if (datum == SLY_TRUE) {
			vector_append(ss, proto->code, iA(OP_LOADTRUE, reg, src_info));
		} else if (null_p(datum)) {
			vector_append(ss, proto->code, iA(OP_LOADNULL, reg, src_info));
		} else if (void_p(datum)) {
			vector_append(ss, proto->code, iA(OP_LOADVOID, reg, src_info));
		} else {
			if (int_p(datum)) {
				i64 i = get_int(datum);
				if (i >= INT16_MIN && i <= INT16_MAX) {
					if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
					vector_append(ss, proto->code, iABx(OP_LOADI, reg, i, src_info));
					return reg;
				}
			}
			size_t idx = intern_constant(ss, datum);
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, idx, src_info));
		}
	}
	return reg;
}

static void
apply_alias(sly_value id, sly_value aliases)
{
	sly_value sym = syntax_to_datum(id);
	syntax *stx = GET_PTR(id);
	sly_value entry = dictionary_entry_ref(aliases, sym);
	stx->context = FLAG_OFF(stx->context, ctx_macro_body);
	if (slot_is_free(entry)) return;
	sly_value alias = cdr(entry);
	stx->alias = sym;
	stx->datum = alias;
}

static void
gen_aliases(Sly_State *ss, sly_value form, sly_value aliases)
{
	if (null_p(form)) return;
	if (syntax_pair_p(form)) {
		while (!null_p(form)) {
			sly_value id = CAR(form);
			sly_assert(identifier_p(id), "Error Expected Identifier in binding form");
			syntax *s = GET_PTR(id);
			if (s->context & ctx_macro_body) {
				sly_value alias = gensym(ss);
				dictionary_set(ss, aliases, syntax_to_datum(id), alias);
				apply_alias(id, aliases);
			}
			form = CDR(form);
		}
	} else {
		sly_value id = form;
		syntax *s = GET_PTR(id);
		if (s->context & ctx_macro_body) {
			sly_value alias = gensym(ss);
			dictionary_set(ss, aliases, syntax_to_datum(id), alias);
			apply_alias(id, aliases);
		}
	}
}

static void
sanitize(Sly_State *ss, sly_value form, sly_value aliases)
{ /* To keep macros hygenic, variable bindings introduced
   * in expansions are replaced by gensyms.
   */
	if (syntax_pair_p(form) && identifier_p(CAR(form))) {
		syntax *s = GET_PTR(CAR(form));
		sly_value sym = s->datum;
		if ((symbol_eq(sym, make_symbol(ss, "define", 6))
			 || symbol_eq(sym, make_symbol(ss, "lambda", 6)))
			&& (s->context & ctx_macro_body)) {
			form = CDR(form);
			gen_aliases(ss, CAR(form), aliases);
			form = CDR(form);
			sanitize(ss, form, aliases);
		} else {
			sanitize(ss, CAR(form), aliases);
			sanitize(ss, CDR(form), aliases);
		}
	} else if (syntax_pair_p(form) || pair_p(form)) {
		sanitize(ss, CAR(form), aliases);
		sanitize(ss, CDR(form), aliases);
	} else if (identifier_p(form)) {
		syntax *s = GET_PTR(form);
		if (s->context & ctx_macro_body) {
			apply_alias(form, aliases);
		}
	}
}

static int
comp_funcall(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	int src_info = -1;
	syntax *stx = GET_PTR(form);
	sly_value head = CAR(form);
	form = CDR(form);
	int start = reg;
	int reg2 = comp_expr(ss, head, reg);
	if (reg2 != -1 && reg2 != reg) {
		src_info = intern_syntax(ss, head);
		vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, src_info));
	}
	reg++;
	while (!null_p(form)) {
		head = CAR(form);
		reg2 = comp_expr(ss, head, reg);
		if (reg2 != -1 && reg2 != reg) {
			src_info = intern_syntax(ss, head);
			vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, src_info));
		}
		reg++;
		form = CDR(form);
	}
	src_info = intern_syntax(ss, (sly_value)stx);
	if (stx->context & ctx_tail_pos) {
		vector_append(ss, proto->code, iAB(OP_TAILCALL, start, reg, src_info));
	} else {
		vector_append(ss, proto->code, iAB(OP_CALL, start, reg, src_info));
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
	sly_value stx = form;
	form = CDR(form);
	int preg = reg;
	prototype *proto = GET_PTR(scope->proto);
	sly_value symtable = scope->symtable;
	sly_value sym, args = CAR(form);
	form = CDR(form);
	union symbol_properties st_prop = {0};
	st_prop.p.type = sym_arg;
	proto->nargs = 0;
	proto->nregs = 0;
	while (pair_p(args) || syntax_pair_p(args)) {
		/* parse parameters */
		sly_value stx = CAR(args);
		sym = syntax_to_datum(stx);
		if (!symbol_p(sym)) {
			sly_display(sym, 1);
			printf("\n");
			sly_raise_exception(ss, EXC_COMPILE, "Compile error function parameter must be a symbol");
		}
		st_prop.p.reg = proto->nregs++;
		proto->nargs++;
		proto->nvars++;
		dictionary_set(ss, symtable, sym, st_prop.v);
		args = CDR(args);
	}
	if (!null_p(args)) {
		/* parse tail arg */
		sym = syntax_to_datum(args);
		if (symbol_p(sym)) {
			proto->has_varg = 1;
			st_prop.p.reg =	proto->nregs++;
			proto->nvars++;
			dictionary_set(ss, symtable, sym, st_prop.v);
		} else {
			sly_display(sym, 1);
			printf("\n");
			sly_raise_exception(ss, EXC_COMPILE, "Compile error function parameter must be a symbol");
		}
	}
	forward_scan_block(ss, form);
	int tmp = reg;
	if (null_p(form)) {
		sly_raise_exception(ss, EXC_COMPILE, "Compile Error empty function body");
	} else {
		while (!null_p(CDR(form))) {
			comp_expr(ss, CAR(form), proto->nvars);
			form = CDR(form);
		}
		syntax *stx = GET_PTR(CAR(form));
		stx->context = FLAG_ON(stx->context, ctx_tail_pos);
		reg = comp_expr(ss, CAR(form), proto->nvars);
	}
	if (reg == -1) reg = tmp;
	vector_append(ss, proto->code, iA(OP_RETURN, reg, -1));
	cc->cscope = cc->cscope->parent;
	reg = preg;
	prototype *cproto = GET_PTR(cc->cscope->proto);
	size_t i = vector_len(cproto->K);
	vector_append(ss, cproto->K, scope->proto);
	if ((size_t)reg >= cproto->nregs) cproto->nregs = reg + 1;
	int src_info = intern_syntax(ss, stx);
	vector_append(ss, cproto->code, iABx(OP_CLOSURE, reg, i, src_info));
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

static void
load_deps(Sly_State *ss, sly_value form)
{
	while (!null_p(form)) {
		sly_value expr = CAR(form);
		if (syntax_pair_p(expr)
			&& identifier_p(CAR(expr))
			&& symbol_eq(syntax_to_datum(CAR(expr)), make_symbol(ss, "load", 4))) {
			load_file(ss, expr);
		}
		form = CDR(form);
	}
}

static void
forward_scan_block(Sly_State *ss, sly_value form)
{ /* Call at start of top-level and procedure block
   * after creating new scope to scan for variables.
   */
	struct scope *scope = ss->cc->cscope;
	sly_value symtable = scope->symtable;
	sly_value globals = ss->cc->globals;
	sly_value expr, name, var;
	prototype *proto = GET_PTR(scope->proto);
	union symbol_properties st_prop = {0};
	st_prop.p.isundefined = 1;
	while (!null_p(form)) {
		expr = CAR(form);
		if (pair_p(expr) || syntax_pair_p(expr)) {
			if (identifier_p(CAR(expr))) {
				name = syntax_to_datum(CAR(expr));
				expr = CDR(expr);
				if (symbol_eq(name, cstr_to_symbol("begin"))) {
					forward_scan_block(ss, expr);
				} else if (symbol_eq(name, cstr_to_symbol("define-syntax"))) {
					name = CAR(expr);
					if (pair_p(name) || syntax_pair_p(name)) {
						name = CAR(name);
					}
					st_prop.p.issyntax = 1;
					goto defvar;
				} else if (symbol_eq(name, cstr_to_symbol("define"))) {
					name = CAR(expr);
					if (pair_p(name) || syntax_pair_p(name)) {
						name = CAR(name);
					}
				    // name := identifier of variable
					// var  := raw symbol of variable
				defvar:
					var = syntax_to_datum(name);
					if (IS_GLOBAL(scope)) {
						st_prop.p.type = sym_global;
						st_prop.p.reg = intern_constant(ss, var);
						dictionary_set(ss, globals, var, SLY_VOID);
					} else {
						st_prop.p.type = sym_variable;
						st_prop.p.reg = proto->nregs++;
						proto->nvars++;
					}
					dictionary_set(ss, symtable, var, st_prop.v);
				}
			}
		}
		form = CDR(form);
	}
}


static sly_value
expand(Sly_State *ss, sly_value form)
{
	if (syntax_pair_p(form)) {
		sly_value head = CAR(form);
		if (identifier_p(head)) {
			syntax *s = GET_PTR(head);
			sly_value macro = lookup_macro(ss, s->datum);
			if (!void_p(macro)) {
				/* call macro */
				closure *clos = GET_PTR(macro);
				prototype *proto = GET_PTR(clos->proto);
				stack_frame *frame = make_stack(ss, proto->nregs);
				frame->U = clos->upvals;
				frame->K = proto->K;
				frame->code = proto->code;
				frame->pc = proto->entry;
				vector_set(frame->R, 0, form);
				ss->frame = frame;
				form = vm_run(ss, 0);
				ss->frame = NULL;
				sly_value aliases = make_dictionary(ss);
				sanitize(ss, form, aliases);
				return expand(ss, form);
			}
		}
		syntax *s = GET_PTR(form);
		s->datum = cons(ss,
						expand(ss, car(s->datum)),
						expand(ss, cdr(s->datum)));
		return form;
	}
	if (pair_p(form)) {
		return cons(ss,
					expand(ss, car(form)),
					expand(ss, cdr(form)));
	}
	return form;
}

static void
load_file(Sly_State *ss, sly_value form)
{
	char *src;
	char path[255] = {0};
	struct scope *pscope = ss->cc->cscope;
	char *ppath = ss->file_path;
	sly_assert(ss->cc->cscope->level == 0,
			   "Error: load form is only allowed in a top-level context");
	form = CDR(form);
	byte_vector *bv = GET_PTR(syntax_to_datum(CAR(form)));
	memcpy(path, bv->elems, bv->len);
	sly_value ast = parse_file(ss, path, &src);
	ss->file_path = path;
	ss->cc->cscope = make_scope(ss);
	ss->cc->cscope->symtable = pscope->symtable;
	ss->cc->cscope->macros = pscope->macros;
	init_symtable(ss, ss->cc->cscope->symtable);
	ss->cc->cscope->level = 0; /* top level */
	sly_compile(ss, ast);
	setup_frame(ss);
	vm_run(ss, 0);
	ss->file_path = ppath;
	ss->cc->cscope = pscope;
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
			reg = comp_lambda(ss, form, reg);
		} break;
		case kw_quote: {
			form = CDR(form);
			if (!null_p(CDR(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed quote");
			}
			form = CAR(form);
			form = strip_syntax(ss, form);
			prototype *proto = GET_PTR(ss->cc->cscope->proto);
			int src_info = intern_syntax(ss, stx);
			if (null_p(form)) {
				vector_append(ss, proto->code, iA(OP_LOADNULL, reg, src_info));
			} else if (form == SLY_TRUE) {
				vector_append(ss, proto->code, iA(OP_LOADTRUE, reg, src_info));
			} else if (form == SLY_FALSE) {
				vector_append(ss, proto->code, iA(OP_LOADFALSE, reg, src_info));
			} else {
				int kreg = intern_constant(ss, form);
				vector_append(ss, proto->code, iABx(OP_LOADK, reg, kreg, src_info));
			}
		} break;
		case kw_syntax_quote: {
			form = CDR(form);
			if (!null_p(CDR(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed quote");
			}
			form = CAR(form);
			prototype *proto = GET_PTR(ss->cc->cscope->proto);
			int src_info = intern_syntax(ss, stx);
			int kreg = intern_constant(ss, form);
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, kreg, src_info));
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
				s->context = FLAG_ON(s->context, ctx_tail_pos);
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
			int src_info = intern_syntax(ss, stx);
			form = CDR(form);
			int reg2 = comp_expr(ss, CAR(form), reg);
			vector_append(ss, proto->code, iAB(OP_CALLWCC, reg, reg2, src_info));
			if (!null_p(CDR(form))) {
				sly_raise_exception(ss, EXC_COMPILE, "Error malformed display expression");
			}
		} break;
		case kw_load: { /* do nothing */
#if 0
			char *src;
			char path[255] = {0};
			struct scope *pscope = ss->cc->cscope;
			char *ppath = ss->file_path;
			sly_assert(ss->cc->cscope->level == 0,
					   "Error: load form is only allowed in a top-level context");
			form = CDR(form);
			byte_vector *bv = GET_PTR(syntax_to_datum(CAR(form)));
			memcpy(path, bv->elems, bv->len);
			sly_value ast = parse_file(ss, path, &src);
			printf("%s\n", ss->file_path);
			ss->file_path = path;
			ss->cc->cscope = make_scope(ss);
			ss->cc->cscope->symtable = pscope->symtable;
			ss->cc->cscope->macros = pscope->macros;
			init_symtable(ss, ss->cc->cscope->symtable);
			ss->cc->cscope->level = 0; /* top level */
			sly_compile(ss, ast);
			setup_frame(ss);
			vm_run(ss, 0);
			ss->file_path = ppath;
			ss->cc->cscope = pscope;
#endif
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
	ss->cc->cscope->level = 0;
	init_builtins(ss);
	define_global_var(ss, make_symbol(ss, "__file__", 8), SLY_FALSE);
	define_global_var(ss, make_symbol(ss, "__name__", 8), SLY_FALSE);
}

static void
setup_frame(Sly_State *ss)
{
	ss->proto = ss->cc->cscope->proto;
	prototype *proto = GET_PTR(ss->proto);
	stack_frame *frame = make_stack(ss, proto->nregs ? proto->nregs : 1);
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
	frame->clos = make_closure(ss, ss->proto);
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
	ss.file_path = file_path;
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
			fprintf(stderr, "Unhandled exception in %s ", ss->file_path);
			fprintf(stderr, "during compilation:\n");
			fprintf(stderr, "%s\n", ss->excpt_msg);
			exit(1);
		});
	prototype *proto = GET_PTR(ss->cc->cscope->proto);
	load_deps(ss, ast);
	ast = expand(ss, ast);
	forward_scan_block(ss, ast);
	int r = comp_expr(ss, ast, 0);
	vector_append(ss, proto->code, iA(OP_RETURN, r, -1));
	END_HANDLE_EXCEPTION(ss);
	return 0;
}
