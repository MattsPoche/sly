#include <assert.h>
#include "sly_types.h"
#include "parser.h"
#include "opcodes.h"
#include "sly_compile.h"

/* TODO
 * [ ] Associate syntactic info with opcodes
 * [ ] Better error messages / vm traps.
 */

enum kw {
	kw_define = 0,
	kw_lambda,
	kw_quote,
	kw_quasiquote,
	kw_unquote,
	kw_unquote_splice,
	kw_syntax_quote,
	kw_syntax_quasiquote,
	kw_syntax_unquote,
	kw_syntax_unquote_splice,
	kw_begin,
	kw_if,
	kw_set,
	kw_display,
	kw_define_syntax,
	kw_syntax_rules,
	kw_call_with_continuation,
	kw_call_cc,
	KW_COUNT,
};

enum symbol_type {
	sym_datum = 0,
	sym_keyword,
	sym_variable,
	sym_arg,
	sym_upval,
	sym_global,
	sym_constant,
};

struct symbol_properties {
	u8 type;
	u8 islocal;
	u8 _pad[2];
	i32 reg;
};

#define SYMPROP_TO_VALUE(prop) (*((sly_value *)(&prop)))
#define VALUE_TO_SYMPROP(val)  (*((struct symbol_properties *)(&(prop))))
#define IS_GLOBAL(scope)       ((scope)->parent == NULL)
#define ADD_BUILTIN(name, fn, nargs, has_vargs)							\
	do {																\
		sym = make_symbol(ss, name, strlen(name));						\
		st_prop.reg = intern_constant(ss, sym);							\
		dictionary_set(ss, symtable, sym, SYMPROP_TO_VALUE(st_prop));	\
		dictionary_set(ss, cc->globals, sym, make_cclosure(ss, fn, nargs, has_vargs)); \
	} while (0)


static char *keywords[KW_COUNT] = {
	[kw_define]					= "define",
	[kw_lambda]					= "lambda",
	[kw_quote]					= "quote",
	[kw_quasiquote]				= "quasiquote",
	[kw_unquote]				= "unquote",
	[kw_unquote_splice]			= "unquote-splice",
	[kw_syntax_quote]			= "syntax-quote",
	[kw_syntax_quasiquote]		= "syntax-quasiquote",
	[kw_syntax_unquote]			= "syntax-unquote",
	[kw_syntax_unquote_splice]	= "syntax-unquote-splice",
	[kw_set]					= "set!",
	[kw_display]				= "display",
	[kw_begin]					= "begin",
	[kw_if]						= "if",
	[kw_define_syntax]			= "define-syntax",
	[kw_syntax_rules]			= "syntax-rules",
	[kw_call_with_continuation] = "call-with-continuation",
	[kw_call_cc]				= "call/cc",
};

static sly_value kw_symbols[KW_COUNT];

int comp_expr(Sly_State *ss, sly_value form, int reg);
static sly_value init_symtable(Sly_State *ss);

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
symbol_lookup_props(struct compile *cc, sly_value sym)
{
	sly_value entry;
	struct scope *scope = cc->cscope;
	while (scope) {
		entry = dictionary_entry_ref(scope->symtable, sym);
		if (!slot_is_free(entry)) {
			return cdr(entry);
		}
		scope = scope->parent;
	}
	sly_assert(0, "Error undefined symbol");
	return SLY_NULL;
}

static size_t
intern_constant(Sly_State *ss, sly_value value)
{
	struct compile *cc = ss->cc;
	prototype *proto = GET_PTR(cc->cscope->proto);
	size_t len = vector_len(proto->K);
	for (size_t i = 0; i < len; ++i) {
		if (sly_eq(value, vector_ref(proto->K, i))) {
			return i;
		}
	}
	vector_append(ss, proto->K, value);
	return len;
}

void
print_syntax(sly_value list)
{
	while (!null_p(list)) {
		if (!pair_p(list)) {
			printf("NOT A PAIR :: %d\n", TYPEOF(list));
		}
		if (pair_p(car(list))) {
			print_syntax(car(list));
		} else if (syntax_p(car(list))) {
			syntax *stx = GET_PTR(car(list));
			printf("[%d, %d] ", stx->tok.ln, stx->tok.cn);
			if (symbol_p(stx->datum)) {
				symbol *sym = GET_PTR(stx->datum);
				printf("\t%.*s ", (int)sym->len, (char *)sym->name);
			} else if (int_p(stx->datum)) {
				i64 n = get_int(stx->datum);
				printf("\t%ld ", n);
			} else if (float_p(stx->datum)) {
				f64 n = get_float(stx->datum);
				printf("\t%g ", n);
			} else if (string_p(stx->datum)) {
				byte_vector *v = GET_PTR(stx->datum);
				printf("\t\"%.*s\" ", (int)v->len, (char *)v->elems);
			}
			printf("\t%s\n", tok_to_string(stx->tok.tag));
		}
		list = cdr(list);
	}
}

static struct scope *
make_scope(Sly_State *ss)
{
	struct scope *scope = malloc(sizeof(*scope));
	assert(scope != NULL);
	scope->parent = NULL;
	scope->proto = make_prototype(ss,
								  make_byte_vector(ss, 0, 8),
								  make_vector(ss, 0, 8),
								  make_vector(ss, 0, 8),
								  0, 0, 0, 0);
	scope->prev_var = 0;
	return scope;
}

static sly_value
init_symtable(Sly_State *ss)
{
	struct symbol_properties prop = { .type = sym_keyword };
	sly_value symtable = make_dictionary(ss);
	for (int i = 0; i < KW_COUNT; ++i) {
		sly_value sym = make_symbol(ss, keywords[i], strlen(keywords[i]));
		dictionary_set(ss, symtable, sym, SYMPROP_TO_VALUE(prop));
		kw_symbols[i] = sym;
	}
	return symtable;
}

int
comp_define(Sly_State *ss, sly_value form, int reg)
{ /* (define <symbol> <expr>) */
	struct compile *cc = ss->cc;
	sly_value stx = car(form);
	syntax *syn = GET_PTR(stx);
	int line_number = syn->tok.ln;
	sly_value var = syntax_to_datum(stx);
	sly_value symtable = cc->cscope->symtable;
	prototype *proto = GET_PTR(cc->cscope->proto);
	struct symbol_properties st_prop = {0};
	sly_assert(symbol_p(var), "Error variable name must be a symbol");
	if (IS_GLOBAL(cc->cscope)) {
		form = cdr(form);
		stx = car(form);
		sly_value globals  = cc->globals;
		sly_value datum = syntax_p(stx) ? syntax_to_datum(stx) : stx;
		st_prop.islocal = 0;
		st_prop.type = sym_global;
		st_prop.reg = intern_constant(ss, var);
		dictionary_set(ss, symtable, var, SYMPROP_TO_VALUE(st_prop));
		if (pair_p(datum) || symbol_p(datum)) {
			dictionary_set(ss, globals, var, SLY_VOID);
			comp_expr(ss, car(form), reg);
			if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg + 1, st_prop.reg, line_number));
			vector_append(ss, proto->code, iABC(OP_SETUPDICT, 0, reg + 1, reg, line_number));
		} else {
			dictionary_set(ss, globals, var, datum);
		}
		sly_assert(null_p(cdr(form)), "Compile Error malformed define");
		return reg;
	} else {
		st_prop.reg = cc->cscope->prev_var++;
		st_prop.islocal = 1;
		st_prop.type = sym_variable;
		dictionary_set(ss, symtable, var, SYMPROP_TO_VALUE(st_prop));
		form = cdr(form);
		comp_expr(ss, car(form), st_prop.reg);
		/* end of definition */
		sly_assert(null_p(cdr(form)), "Compile Error malformed define");
		return st_prop.reg;
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
	sly_assert(null_p(form), "Compile Error malformed if expression");
	int be_res = comp_expr(ss, boolexpr, reg);
	size_t fjmp = vector_len(proto->code);
	vector_append(ss, proto->code, 0);
	comp_expr(ss, tbranch, reg);
	size_t jmp = vector_len(proto->code);
	vector_append(ss, proto->code, 0);
	comp_expr(ss, fbranch, reg);
	size_t end = vector_len(proto->code);
	if (be_res == -1) {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, reg, jmp + 1, -1));
	} else {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, be_res, jmp + 1, -1));
	}
	vector_set(proto->code, jmp,  iAx(OP_JMP, end, -1));
	return reg;
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
	sly_assert(symbol_p(datum), "Error must be a variable");
	sly_value prop = symbol_lookup_props(cc, datum);
	struct symbol_properties st_prop = VALUE_TO_SYMPROP(prop);
	prototype *proto = GET_PTR(cc->cscope->proto);
	form = cdr(form);
	reg = comp_expr(ss, car(form), reg);
	if ((size_t)reg + 1 <= proto->nregs) proto->nregs = reg + 2;
	if (st_prop.type == sym_variable) {
		if (reg != -1 && st_prop.reg != reg) {
			vector_append(ss, proto->code, iAB(OP_MOVE, st_prop.reg, reg, line_number));
		}
	} else if (st_prop.type == sym_global) {
		vector_append(ss, proto->code, iABx(OP_LOADK, reg + 1, st_prop.reg, line_number));
		vector_append(ss, proto->code, iABC(OP_SETUPDICT, 0, reg + 1, reg, line_number));
	}
	sly_assert(null_p(cdr(form)), "Error malformed set! expression");
	return reg;
}

int
comp_atom(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	sly_value datum;
	datum = syntax_to_datum(form);
	syntax *syn = GET_PTR(form);
	int line_number = syn->tok.ln;
	prototype *proto = GET_PTR(cc->cscope->proto);
	if (symbol_p(datum)) {
		sly_value prop = symbol_lookup_props(cc, datum);
		struct symbol_properties st_prop = VALUE_TO_SYMPROP(prop);
		switch ((enum symbol_type)st_prop.type) {
		case sym_variable: {
			return st_prop.reg;
		} break;
		case sym_constant: {
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, st_prop.reg, line_number));
		} break;
		case sym_datum: {
		} break;
		case sym_keyword: {
		} break;
		case sym_arg:
		case sym_upval: {
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iAB(OP_GETUPVAL, reg, st_prop.reg, line_number));
		} break;
		case sym_global: {
			if (!IS_GLOBAL(cc->cscope)) {
				st_prop.islocal = 1;
				st_prop.reg = intern_constant(ss, datum);
				dictionary_set(ss, cc->cscope->symtable, datum, SYMPROP_TO_VALUE(st_prop));
			}
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(ss, proto->code, iABx(OP_LOADK, reg, st_prop.reg, line_number));
			vector_append(ss, proto->code, iABC(OP_GETUPDICT, reg, 0, reg, line_number));
		} break;
		}
	} else { /* constant */
		if (datum == SLY_FALSE) {
			vector_append(ss, proto->code, iA(OP_LOAD_FALSE, reg, line_number));
		} else if (datum == SLY_TRUE) {
			vector_append(ss, proto->code, iA(OP_LOAD_TRUE, reg, line_number));
		} else if (null_p(datum)) {
			vector_append(ss, proto->code, iA(OP_LOAD_NULL, reg, line_number));
		} else if (void_p(datum)) {
			vector_append(ss, proto->code, iA(OP_LOAD_VOID, reg, line_number));
		} else {
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
	int start = reg;
	int reg2 = comp_expr(ss, head, reg);
	if (reg2 != -1 && reg2 != reg) {
		vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, -1));
	}
	reg++;
	while (pair_p(form)) {
		head = car(form);
		reg2 = comp_expr(ss, head, reg);
		if (reg2 != -1 && reg2 != reg) {
			vector_append(ss, proto->code, iAB(OP_MOVE, reg, reg2, -1));
		}
		reg++;
		form = cdr(form);
	}
	vector_append(ss, proto->code, iAB(OP_CALL, start, reg, -1));
	return reg;
}

int
comp_lambda(Sly_State *ss, sly_value form, int reg)
{
	struct compile *cc = ss->cc;
	struct scope *scope = make_scope(ss);
	scope->symtable = init_symtable(ss);
	scope->parent = cc->cscope;
	cc->cscope = scope;
	int preg = reg;
	reg = 0;
	prototype *proto = GET_PTR(scope->proto);
	sly_value symtable = scope->symtable;
	sly_value stx, sym, args = car(form);
	form = cdr(form);
	struct symbol_properties st_prop = {0};
	st_prop.type = sym_arg;
	st_prop.islocal = 1;
	size_t nargs = 0;
	while (pair_p(args)) {
		stx = car(args);
		sym = syntax_to_datum(stx);
		sly_assert(symbol_p(sym), "Compile error function parameter must be a symbol");
		nargs++;
		st_prop.reg = nargs;
		dictionary_set(ss, symtable, sym, SYMPROP_TO_VALUE(st_prop));
		args = cdr(args);
	}
	if (!null_p(args)) {
		sym = syntax_to_datum(args);
		sly_assert(symbol_p(sym), "Compile error function parameter must be a symbol");
		proto->has_varg = 1;
		st_prop.reg = nargs + 1;
		dictionary_set(ss, symtable, sym, SYMPROP_TO_VALUE(st_prop));
	}
	proto->nargs = nargs;
	while (!null_p(form)) {
		reg = comp_expr(ss, car(form), reg);
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
	free(scope);
	return reg;
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
		sly_assert(0, "Error Unemplemented");
	}
	datum = syntax_to_datum(stx);
	int kw;
	if (symbol_p(datum) && (kw = is_keyword(datum)) != -1) {
		switch ((enum kw)kw) {
		case kw_define: {
			form = cdr(form);
			reg = comp_define(ss, form, reg);
		} break;
		case kw_lambda: {
			form = cdr(form);
			reg = comp_lambda(ss, form, reg);
		} break;
		case kw_quote: {
		} break;
		case kw_quasiquote: {
		} break;
		case kw_unquote: {
		} break;
		case kw_unquote_splice: {
		} break;
		case kw_syntax_quote: {
		} break;
		case kw_syntax_quasiquote: {
		} break;
		case kw_syntax_unquote: {
		} break;
		case kw_syntax_unquote_splice: {
		} break;
		case kw_begin: {
			form = cdr(form);
			while (!null_p(form)) {
				reg = comp_expr(ss, car(form), reg);
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
		case kw_display: {
			prototype *proto = GET_PTR(cc->cscope->proto);
			form = cdr(form);
			int reg2 = comp_expr(ss, car(form), reg);
			if (reg2 == -1) {
				vector_append(ss, proto->code, iA(OP_DISPLAY, reg, -1));
			} else {
				vector_append(ss, proto->code, iA(OP_DISPLAY, reg2, -1));
			}
			sly_assert(null_p(cdr(form)), "Error malformed display expression");
		} break;
		case kw_define_syntax: {
		} break;
		case kw_syntax_rules: {
		} break;
		case kw_call_with_continuation:
		case kw_call_cc: {
		} break;
		case KW_COUNT: {
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
	sly_value total = sly_add(ss, vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_add(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
csub(Sly_State *ss, sly_value args)
{
	sly_value total = sly_sub(ss, vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_sub(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cmul(Sly_State *ss, sly_value args)
{
	sly_value total = sly_mul(ss, vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_mul(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cdiv(Sly_State *ss, sly_value args)
{
	sly_value total = sly_div(ss, vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_div(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
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
	(void)ss;
	return ctobool(sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_lt(Sly_State *ss, sly_value args)
{
	(void)ss;
	return ctobool(sly_num_lt(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_gt(Sly_State *ss, sly_value args)
{
	(void)ss;
	return ctobool(sly_num_gt(vector_ref(args, 0), vector_ref(args, 1)));
}

void
init_builtins(Sly_State *ss)
{ /* TODO The names of these symbols do not need to be added into
   * constants if they are never referenced.
   */
	struct compile *cc = ss->cc;
	sly_value symtable = cc->cscope->symtable;
	struct symbol_properties st_prop = {0};
	sly_value sym;
	st_prop.islocal = 0;
	st_prop.type = sym_global;
	cc->globals = make_dictionary(ss);
	ADD_BUILTIN("+", cadd, 2, 1);
	ADD_BUILTIN("-", csub, 2, 1);
	ADD_BUILTIN("*", cmul, 2, 1);
	ADD_BUILTIN("/", cdiv, 2, 1);
	ADD_BUILTIN("%", cmod, 2, 1);
	ADD_BUILTIN("=", cnum_eq, 2, 0);
	ADD_BUILTIN("<", cnum_lt, 2, 0);
	ADD_BUILTIN(">", cnum_gt, 2, 0);
}

void
sly_compile(Sly_State *ss, char *file_name)
{
	char *src;
	sly_value ast;
	prototype *proto;
	ss->cc = malloc(sizeof(*ss->cc));
	assert(ss->cc != NULL);
	struct compile *cc = ss->cc;
	cc->cscope = make_scope(ss);
	cc->interned = make_dictionary(ss);
	cc->cscope->symtable = init_symtable(ss);
	init_builtins(ss);
	ast = parse_file(ss, file_name, &src);
	proto = GET_PTR(cc->cscope->proto);
	int r = comp_expr(ss, ast, 0);
	vector_append(ss, proto->code, iA(OP_RETURN, r, -1));
}
