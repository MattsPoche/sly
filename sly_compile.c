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
		sym = make_symbol(cc->interned, name, strlen(name));			\
		st_prop.reg = intern_constant(cc, sym);							\
		dictionary_set(symtable, sym, SYMPROP_TO_VALUE(st_prop));		\
		dictionary_set(cc->globals, sym, make_cclosure(fn, nargs, has_vargs)); \
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

int comp_expr(struct compile *cc, sly_value form, int reg);
static sly_value init_symtable(sly_value interned);

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
intern_constant(struct compile *cc, sly_value value)
{
	prototype *proto = GET_PTR(cc->cscope->proto);
	size_t len = vector_len(proto->K);
	for (size_t i = 0; i < len; ++i) {
		if (sly_eq(value, vector_ref(proto->K, i))) {
			return i;
		}
	}
	vector_append(proto->K, value);
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
make_scope(void)
{
	struct scope *scope = sly_alloc(sizeof(*scope));
	scope->parent = NULL;
	scope->proto = make_prototype(make_byte_vector(0, 8),
								  make_vector(0, 8),
								  make_vector(0, 8),
								  0, 0, 0, 0);
	scope->prev_var = 0;
	return scope;
}

static sly_value
init_symtable(sly_value interned)
{
	struct symbol_properties prop = { .type = sym_keyword };
	sly_value symtable = make_dictionary();
	for (int i = 0; i < KW_COUNT; ++i) {
		sly_value sym = make_symbol(interned, keywords[i], strlen(keywords[i]));
		dictionary_set(symtable, sym, SYMPROP_TO_VALUE(prop));
		kw_symbols[i] = sym;
	}
	return symtable;
}

int
comp_define(struct compile *cc, sly_value form, int reg)
{ /* (define <symbol> <expr>) */
	sly_value stx = car(form);
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
		st_prop.reg = intern_constant(cc, var);
		dictionary_set(symtable, var, SYMPROP_TO_VALUE(st_prop));
		if (pair_p(datum) || symbol_p(datum)) {
			dictionary_set(globals, var, SLY_VOID);
			comp_expr(cc, car(form), reg);
			if ((size_t)reg + 1 >= proto->nregs) proto->nregs = reg + 2;
			vector_append(proto->code, iABx(OP_LOADK, reg + 1, st_prop.reg));
			vector_append(proto->code, iABC(OP_SETUPDICT, 0, reg + 1, reg));
		} else {
			dictionary_set(globals, var, datum);
		}
		sly_assert(null_p(cdr(form)), "Compile Error malformed define");
		return reg;
	} else {
		st_prop.reg = cc->cscope->prev_var++;
		st_prop.islocal = 1;
		st_prop.type = sym_variable;
		dictionary_set(symtable, var, SYMPROP_TO_VALUE(st_prop));
		form = cdr(form);
		comp_expr(cc, car(form), st_prop.reg);
		/* end of definition */
		sly_assert(null_p(cdr(form)), "Compile Error malformed define");
		return st_prop.reg;
	}
}

int
comp_if(struct compile *cc, sly_value form, int reg)
{
	prototype *proto = GET_PTR(cc->cscope->proto);
	sly_value boolexpr = car(form);
	form = cdr(form);
	sly_value tbranch = car(form);
	form = cdr(form);
	sly_value fbranch = car(form);
	form = cdr(form);
	sly_assert(null_p(form), "Compile Error malformed if expression");
	int be_res = comp_expr(cc, boolexpr, reg);
	size_t fjmp = vector_len(proto->code);
	vector_append(proto->code, 0);
	comp_expr(cc, tbranch, reg);
	size_t jmp = vector_len(proto->code);
	vector_append(proto->code, 0);
	comp_expr(cc, fbranch, reg);
	size_t end = vector_len(proto->code);
	if (be_res == -1) {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, reg, jmp + 1));
	} else {
		vector_set(proto->code, fjmp, iABx(OP_FJMP, be_res, jmp + 1));
	}
	vector_set(proto->code, jmp,  iAx(OP_JMP, end));
	return reg;
}

int
comp_set(struct compile *cc, sly_value form, int reg)
{
	form = cdr(form);
	sly_value stx = car(form);
	sly_value datum = syntax_to_datum(stx);
	sly_assert(symbol_p(datum), "Error must be a variable");
	sly_value prop = symbol_lookup_props(cc, datum);
	struct symbol_properties st_prop = VALUE_TO_SYMPROP(prop);
	prototype *proto = GET_PTR(cc->cscope->proto);
	form = cdr(form);
	reg = comp_expr(cc, car(form), reg);
	if ((size_t)reg + 1 <= proto->nregs) proto->nregs = reg + 2;
	if (st_prop.type == sym_variable) {
		if (reg != -1 && st_prop.reg != reg) {
			vector_append(proto->code, iAB(OP_MOVE, st_prop.reg, reg));
		}
	} else if (st_prop.type == sym_global) {
		vector_append(proto->code, iAB(OP_LOADK, reg + 1, st_prop.reg));
		vector_append(proto->code, iABC(OP_SETUPDICT, 0, reg + 1, reg));
	}
	sly_assert(null_p(cdr(form)), "Error malformed set! expression");
	return reg;
}

int
comp_atom(struct compile *cc, sly_value form, int reg)
{
	sly_value datum;
	datum = syntax_to_datum(form);
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
			vector_append(proto->code, iABx(OP_LOADK, reg, st_prop.reg));
		} break;
		case sym_datum: {
		} break;
		case sym_keyword: {
		} break;
		case sym_arg:
		case sym_upval: {
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(proto->code, iAB(OP_GETUPVAL, reg, st_prop.reg));
		} break;
		case sym_global: {
			if (!IS_GLOBAL(cc->cscope)) {
				st_prop.islocal = 1;
				st_prop.reg = intern_constant(cc, datum);
				dictionary_set(cc->cscope->symtable, datum, SYMPROP_TO_VALUE(st_prop));
			}
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(proto->code, iABx(OP_LOADK, reg, st_prop.reg));
			vector_append(proto->code, iABC(OP_GETUPDICT, reg, 0, reg));
		} break;
		}
	} else { /* constant */
		if (datum == SLY_FALSE) {
			vector_append(proto->code, iA(OP_LOAD_FALSE, reg));
		} else if (datum == SLY_TRUE) {
			vector_append(proto->code, iA(OP_LOAD_TRUE, reg));
		} else if (null_p(datum)) {
			vector_append(proto->code, iA(OP_LOAD_NULL, reg));
		} else if (void_p(datum)) {
			vector_append(proto->code, iA(OP_LOAD_VOID, reg));
		} else {
			size_t idx = intern_constant(cc, datum);
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(proto->code, iABx(OP_LOADK, reg, idx));
		}
	}
	return -1;
}

int
comp_funcall(struct compile *cc, sly_value form, int reg)
{
	prototype *proto = GET_PTR(cc->cscope->proto);
	sly_value head = car(form);
	form = cdr(form);
	int start = reg;
	int reg2 = comp_expr(cc, head, reg);
	if (reg2 != -1 && reg2 != reg) {
		vector_append(proto->code, iAB(OP_MOVE, reg, reg2));
	}
	reg++;
	while (pair_p(form)) {
		head = car(form);
		reg2 = comp_expr(cc, head, reg);
		if (reg2 != -1 && reg2 != reg) {
			vector_append(proto->code, iAB(OP_MOVE, reg, reg2));
		}
		reg++;
		form = cdr(form);
	}
	vector_append(proto->code, iAB(OP_CALL, start, reg));
	return reg;
}

int
comp_lambda(struct compile *cc, sly_value form, int reg)
{
	struct scope *scope = make_scope();
	scope->symtable = init_symtable(cc->interned);
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
		dictionary_set(symtable, sym, SYMPROP_TO_VALUE(st_prop));
		args = cdr(args);
	}
	if (!null_p(args)) {
		sym = syntax_to_datum(args);
		sly_assert(symbol_p(sym), "Compile error function parameter must be a symbol");
		proto->has_varg = 1;
		st_prop.reg = nargs + 1;
		dictionary_set(symtable, sym, SYMPROP_TO_VALUE(st_prop));
	}
	proto->nargs = nargs;
	while (!null_p(form)) {
		reg = comp_expr(cc, car(form), reg);
		form = cdr(form);
	}
	vector_append(proto->code, iA(OP_RETURN, reg));
	cc->cscope = cc->cscope->parent;
	reg = preg;
	prototype *cproto = GET_PTR(cc->cscope->proto);
	size_t i = vector_len(cproto->K);
	vector_append(cproto->K, scope->proto);
	if ((size_t)reg >= cproto->nregs) cproto->nregs = reg + 1;
	vector_append(cproto->code, iABx(OP_CLOSURE, reg, i));
	free(scope);
	return reg;
}

int
comp_expr(struct compile *cc, sly_value form, int reg)
{
	if (!pair_p(form)) {
		return comp_atom(cc, form, reg);
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
			reg = comp_define(cc, form, reg);
		} break;
		case kw_lambda: {
			form = cdr(form);
			reg = comp_lambda(cc, form, reg);
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
				reg = comp_expr(cc, car(form), reg);
				form = cdr(form);
			}
		} break;
		case kw_if: {
			form = cdr(form);
			reg = comp_if(cc, form, reg);
		} break;
		case kw_set: {
			comp_set(cc, form, reg);
		} break;
		case kw_display: {
			prototype *proto = GET_PTR(cc->cscope->proto);
			form = cdr(form);
			int reg2 = comp_expr(cc, car(form), reg);
			if (reg2 == -1) {
				vector_append(proto->code, iA(OP_DISPLAY, reg));
			} else {
				vector_append(proto->code, iA(OP_DISPLAY, reg2));
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
		comp_funcall(cc, form, reg);
	}
	return reg;
}

sly_value
cadd(sly_value args)
{
	sly_value total = sly_add(vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_add(total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
csub(sly_value args)
{
	sly_value total = sly_sub(vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_sub(total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cmul(sly_value args)
{
	sly_value total = sly_mul(vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_mul(total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cdiv(sly_value args)
{
	sly_value total = sly_div(vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_div(total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cmod(sly_value args)
{
	sly_value total = sly_mod(vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_mod(total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

sly_value
cnum_eq(sly_value args)
{
	return ctobool(sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_lt(sly_value args)
{
	return ctobool(sly_num_lt(vector_ref(args, 0), vector_ref(args, 1)));
}

sly_value
cnum_gt(sly_value args)
{
	return ctobool(sly_num_gt(vector_ref(args, 0), vector_ref(args, 1)));
}

void
init_builtins(struct compile *cc)
{ /* TODO The names of these symbols do not need to be added into
   * constants if they are never referenced.
   */
	sly_value symtable = cc->cscope->symtable;
	struct symbol_properties st_prop = {0};
	sly_value sym;
	st_prop.islocal = 0;
	st_prop.type = sym_global;
	cc->globals = make_dictionary();
	ADD_BUILTIN("+", cadd, 2, 1);
	ADD_BUILTIN("-", csub, 2, 1);
	ADD_BUILTIN("*", cmul, 2, 1);
	ADD_BUILTIN("/", cdiv, 2, 1);
	ADD_BUILTIN("%", cmod, 2, 1);
	ADD_BUILTIN("=", cnum_eq, 2, 0);
	ADD_BUILTIN("<", cnum_lt, 2, 0);
	ADD_BUILTIN(">", cnum_gt, 2, 0);
}

struct compile
sly_compile(char *file_name)
{
	char *src;
	sly_value ast;
	struct compile cc;
	prototype *proto;
	cc.cscope = make_scope();
	cc.interned = make_dictionary();
	cc.cscope->symtable = init_symtable(cc.interned);
	init_builtins(&cc);
	ast = parse_file(file_name, &src, cc.interned);
	proto = GET_PTR(cc.cscope->proto);
	int r = comp_expr(&cc, ast, 0);
	vector_append(proto->code, iA(OP_RETURN, r));
	return cc;
}
