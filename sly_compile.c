#include "sly_types.h"
#include "parser.h"
#include "opcodes.h"
#include "sly_compile.h"

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
	scope->symtable = make_dictionary();
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
		st_prop.reg = vector_len(proto->K);
		dictionary_set(symtable, var, SYMPROP_TO_VALUE(st_prop));
		vector_append(proto->K, var);
		if (pair_p(datum) || symbol_p(datum)) {
			dictionary_set(globals, var, SLY_VOID);
			comp_expr(cc, car(form), reg);
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
comp_set(struct compile *cc, sly_value form, int reg)
{
	form = cdr(form);
	sly_value stx = car(form);
	sly_value datum = syntax_to_datum(stx);
	sly_assert(symbol_p(datum), "Error must be a variable");
	sly_value prop = dictionary_ref(cc->cscope->symtable, datum);
	struct symbol_properties st_prop = VALUE_TO_SYMPROP(prop);
	prototype *proto = GET_PTR(cc->cscope->proto);
	form = cdr(form);
	reg = comp_expr(cc, car(form), reg);
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
		sly_value prop = dictionary_ref(cc->cscope->symtable, datum);
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
			if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
			vector_append(proto->code, iABx(OP_LOADK, reg, st_prop.reg));
			vector_append(proto->code, iABC(OP_GETUPDICT, reg, 0, reg));
		} break;
		}
	} else { /* constant */
		size_t idx = vector_len(proto->K);
		vector_append(proto->K, datum);
		if ((size_t)reg >= proto->nregs) proto->nregs = reg + 1;
		vector_append(proto->code, iABx(OP_LOADK, reg, idx));
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
			comp_define(cc, form, reg);
		} break;
		case kw_lambda: {
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
	sly_value sum = sly_add(vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		sum = sly_add(sum, car(vargs));
		vargs = cdr(vargs);
	}
	return sum;
}

void
init_builtins(struct compile *cc)
{

	prototype *proto = GET_PTR(cc->cscope->proto);
	sly_value symtable = cc->cscope->symtable;
	struct symbol_properties st_prop = {0};
	cc->globals = make_dictionary();
	st_prop.reg = vector_len(proto->K);
	st_prop.islocal = 0;
	st_prop.type = sym_global;
	sly_value sym = make_symbol(cc->interned, "+", 1);
	vector_append(proto->K, sym);
	dictionary_set(symtable, sym, SYMPROP_TO_VALUE(st_prop));
	dictionary_set(cc->globals, sym, make_cclosure(cadd, 2, 1));
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
	comp_expr(&cc, ast, 0);
	vector_append(proto->code, iA(OP_RETURN, 0));
	return cc;
}
