#include <assert.h>
#include <stdio.h>
#include <gc.h>
#include <time.h>
#include <stdarg.h>
#include "sly_types.h"
#include "parser.h"
#include "sly_compile.h"
#include "opcodes.h"
#include "eval.h"
#include "sly_vm.h"
//#include "intmap.h"

#define CAR(val) (syntax_p(val) ? car(syntax_to_datum(val)) : car(val))
#define CDR(val) (syntax_p(val) ? cdr(syntax_to_datum(val)) : cdr(val))

/* typedef u32 cps_variable; */
/* typedef u32 cps_label; */

enum cps_type {
	tt_cps_const,
	tt_cps_call,
	tt_cps_kcall,
	tt_cps_proc,
	tt_cps_prim,
	tt_cps_primcall,
	tt_cps_values,
	tt_cps_branch,
	tt_cps_continue,
	tt_cps_kargs,
	tt_cps_kreceive,
	tt_cps_kproc,
	tt_cps_ktail,
};

enum prim {
	prim_null = 0,
	prim_add,
	prim_sub,
	prim_mul,
	prim_div,
	prim_idiv,
	prim_mod,
	prim_bw_and,
	prim_bw_ior,
	prim_bw_xor,
	prim_bw_eqv,
	prim_bw_nor,
	prim_bw_nand,
	prim_bw_not,
	prim_bw_shift,
	prim_void,
	prim_apply,
};

struct primop {
	char *cstr;
	sly_value name;
};

struct arity_t {
	sly_value req;
	sly_value rest;
};

/* continuations */
typedef struct _cps_kargs {
	sly_value vars;          // list of variables
	struct _cps_term *term;
} CPS_Kargs;

typedef struct _cps_kreceive {
	struct arity_t arity;
	sly_value k;            // symbol
} CPS_Kreceive;

typedef struct _cps_Kproc {
	struct arity_t arity;
	sly_value tail;
	sly_value body;
} CPS_Kproc;

typedef struct _cps_cont {
	int type;
	sly_value name;  // symbol
	union {
		CPS_Kargs kargs;
		CPS_Kreceive kreceive;
		CPS_Kproc kproc;
	} u;
} CPS_Kont;

/* terms */
typedef struct _cps_continue {
	sly_value k;
	struct _cps_expr *expr;
} CPS_Continue;

typedef struct _cps_branch {
	sly_value arg;
	sly_value kt;
	sly_value kf;
} CPS_Branch;

typedef struct _cps_term {
	int type;
	union {
		CPS_Continue cont;
		CPS_Branch branch;
	} u;
} CPS_Term;

/* expressions */
typedef struct _cps_values {
	sly_value args;   // list of variables
} CPS_Values;

typedef struct _cps_const {
	sly_value value;
} CPS_Const;

typedef struct _cps_proc {
	sly_value k;    // kfun
} CPS_Proc;

typedef struct _cps_prim {
	sly_value name;
} CPS_Prim;

typedef struct _cps_primcall {
	sly_value prim;
	sly_value args;
} CPS_Primcall;

typedef struct _cps_call {
	sly_value proc;
	sly_value args;
} CPS_Call;

typedef struct _cps_kcall {
	sly_value proc_name;
	sly_value label;
	sly_value args;
} CPS_Kcall;

typedef struct _cps_expr {
	int type;
	union {
		CPS_Call call;
		CPS_Kcall kcall;
		CPS_Const constant;
		CPS_Proc proc;
		CPS_Prim prim;
		CPS_Primcall primcall;
		CPS_Values values;
	} u;
} CPS_Expr;

CPS_Kont *cps_graph_ref(sly_value graph, sly_value k);
void cps_graph_set(Sly_State *ss, sly_value graph, sly_value k, CPS_Kont *kont);
CPS_Expr *cps_binding_ref(sly_value bindings, sly_value v);
void cps_binding_update(Sly_State *ss, sly_value bindings, sly_value v, CPS_Expr *e);
int cps_graph_is_member(sly_value graph, sly_value k);
CPS_Term *cps_new_term(void);
CPS_Expr *cps_make_constant(sly_value value);
CPS_Expr *cps_new_expr(void);
sly_value cps_gensym_temporary_name(Sly_State *ss);
sly_value cps_gensym_label_name(Sly_State *ss);
CPS_Kont *cps_make_kargs(Sly_State *ss, sly_value name, CPS_Term *term, sly_value vars);
CPS_Kont *cps_make_ktail(Sly_State *ss, int genname);
void cps_init_primops(Sly_State *ss);
sly_value cps_translate(Sly_State *ss, sly_value cc, sly_value graph, sly_value form);
void cps_display(Sly_State *ss, sly_value graph, sly_value k);

static struct primop primops[] = {
	[prim_null]		= {NULL},
	[prim_add]		= {"+"},                     // (+ . args)   ; addition
	[prim_sub]		= {"-"},                     // (- . args)   ; subtraction
	[prim_mul]		= {"*"},                     // (* . args)   ; multiplication
	[prim_div]		= {"/"},                     // (/ . args)   ; real division
	[prim_idiv]		= {"div"},                   // (div . args) ; integer division
	[prim_mod]		= {"%"},                     // (% . args)   ; mod
	[prim_bw_and]	= {"bitwise-and"},           // (bitwise-and x y)
	[prim_bw_ior]	= {"bitwise-ior"},           // (bitwise-ior x y)
	[prim_bw_xor]	= {"bitwise-xor"},           // (bitwise-xor x y)
	[prim_bw_eqv]	= {"bitwise-eqv"},           // (bitwise-eqv x y)
	[prim_bw_nor]	= {"bitwise-nor"},           // (bitwise-nor x y)
	[prim_bw_nand]	= {"bitwise-nand"},          // (bitwise-nand x y)
	[prim_bw_not]	= {"bitwise-not"},           // (bitwise-not x y)
	[prim_bw_shift] = {"arithmetic-shift"},      // (bitwise-shift x y)
	[prim_void]		= {"void"},                  // (void) ; #<void>
	[prim_apply]	= {"apply"},
};

void
cps_init_primops(Sly_State *ss)
{
	for (size_t i = 1; i < ARR_LEN(primops); ++i) {
		char *cstr = primops[i].cstr;
		primops[i].name = make_symbol(ss, cstr, strlen(cstr));
	}
}

static int
primop_p(sly_value name)
{
	if (identifier_p(name)) {
		name = strip_syntax(name);
	} else if (!symbol_p(name)) {
		return 0;
	}
	for (size_t i = 1; i < ARR_LEN(primops); ++i) {
		if (symbol_eq(name, primops[i].name)) {
			return i;
		}
	}
	return 0;
}

CPS_Expr *
cps_binding_ref(sly_value bindings, sly_value v)
{
	sly_value x = dictionary_ref(bindings, v, SLY_FALSE);
	sly_assert(x != SLY_FALSE, "Error variable undefined");
	return GET_PTR(x);
}

void
cps_binding_update(Sly_State *ss, sly_value bindings, sly_value v, CPS_Expr *e)
{
	dictionary_set(ss, bindings, v, (sly_value)e);
}

CPS_Kont *
cps_graph_ref(sly_value graph, sly_value k)
{
	sly_value x = dictionary_ref(graph, k, SLY_FALSE);
	sly_assert(x != SLY_FALSE, "Error label not set");
	return GET_PTR(x);
}

int
cps_graph_is_member(sly_value graph, sly_value k)
{
	return dictionary_ref(graph, k, SLY_FALSE) != SLY_FALSE;

}

void
cps_graph_set(Sly_State *ss, sly_value graph, sly_value k, CPS_Kont *kont)
{
	dictionary_set(ss, graph, k, (sly_value)kont);
}

CPS_Term *
cps_new_term(void)
{
	CPS_Term *t = GC_MALLOC(sizeof(*t));
	t->type = tt_cps_continue;
	return t;
}

CPS_Expr *
cps_make_constant(sly_value value)
{
	CPS_Expr *e = cps_new_expr();
	e->u.constant.value = value;
	return e;
}

CPS_Expr *
cps_new_expr(void)
{
	CPS_Expr *e = GC_MALLOC(sizeof(*e));
	e->type = tt_cps_const;
	return e;
}

CPS_Kont *
cps_make_ktail(Sly_State *ss, int genname)
{
	CPS_Kont *c = GC_MALLOC(sizeof(*c));
	c->type = tt_cps_ktail;
	if (genname) {
		c->name = gensym_from_cstr(ss, "$ktail");
	}
	return c;
}

sly_value
cps_gensym_temporary_name(Sly_State *ss)
{
	return gensym_from_cstr(ss, "$t");
}

sly_value
cps_gensym_label_name(Sly_State *ss)
{
	return gensym_from_cstr(ss, "$k");
}

CPS_Kont *
cps_make_kargs(Sly_State *ss, sly_value name, CPS_Term *term, sly_value vars)
{
	CPS_Kont *k = cps_make_ktail(ss, 0);
	k->type = tt_cps_kargs;
	k->name = name;
	k->u.kargs.term = term;
	k->u.kargs.vars = vars;
	return k;
}

void
cps_visit_expr(CPS_Expr *expr, sly_value graph, sly_value in, sly_value successor)
{
}

void
cps_visit_kont(Sly_State *ss, CPS_Kont *kont, sly_value graph, sly_value state)
{
	sly_value successor;
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		switch (term->type) {
		case tt_cps_branch: {
			sly_assert(0, "unimplemented");
		} break;
		case tt_cps_continue: {
			successor = term->u.cont.k;
			term->u.cont.expr
		} break;
		}
	} break;
	case tt_cps_kreceive: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_kproc: {
		sly_assert(0, "unimplemented");
	} break;
	}
}

void
cps_get_bindings(Sly_State *ss, sly_value graph, sly_value k)
{
	sly_value state = make_dictionary(ss);
	CPS_Kont *kont = cps_graph_ref(graph, k);
	cps_visit_kont(ss, kont, graph, state);
}

#define CPS_CONSTANT()													\
	do {																\
		t = cps_new_term();												\
		t->type = tt_cps_continue;										\
		if (syntax_quote) {												\
			t->u.cont.expr = cps_make_constant(form);					\
		} else {														\
			t->u.cont.expr = cps_make_constant(strip_syntax(form));		\
		}																\
		t->u.cont.k = cc;												\
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t,			\
						   make_list(ss, 1, cps_gensym_temporary_name(ss))); \
		cc = k->name;													\
		cps_graph_set(ss, graph, cc, k);								\
	} while (0)

sly_value
cps_translate(Sly_State *ss, sly_value cc, sly_value graph, sly_value form)
{
	if (null_p(form)) {
		return cc;
	}
	int syntax_quote = 0;
	CPS_Term *t;
	CPS_Kont *k;
	if (syntax_pair_p(form) || pair_p(form)) {
		sly_value fst = CAR(form);
		sly_value rest = CDR(form);
		if (identifier_p(fst) || symbol_p(fst)) {
			if (symbol_eq(strip_syntax(fst), make_symbol(ss, "define", 6))
				|| symbol_eq(strip_syntax(fst), make_symbol(ss, "define-syntax", 13))
				|| symbol_eq(strip_syntax(fst), make_symbol(ss, "set!", 4))) {
				sly_value name = strip_syntax(CAR(rest));
				sly_value kname = cps_gensym_label_name(ss);
				sly_value kk = cps_translate(ss, kname, graph, CAR(CDR(rest)));
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_values;
				sly_value args = make_list(ss, 1, name);
				t->u.cont.expr->u.values.args = args;
				t->u.cont.k = cc;
				k = cps_make_kargs(ss, kname, t, args);
				cps_graph_set(ss, graph, kname, k);
				return kk;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "call/cc", 7))
					   || symbol_eq(strip_syntax(fst),
									make_symbol(ss, "call-with-current-continuation", 30))) {
				sly_value e = CAR(rest);
				sly_value proc;
				k = cps_make_ktail(ss, 0);
				k->type = tt_cps_kreceive;
				k->name = cps_gensym_label_name(ss);
				k->u.kreceive.k = cc;
				k->u.kreceive.arity.req = cons(ss,
											   cps_gensym_temporary_name(ss),
											   SLY_NULL);
				k->u.kreceive.arity.rest = SLY_FALSE;
				cc = k->name;
				cps_graph_set(ss, graph, cc, k);
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_call;
				t->u.cont.expr->u.call.args = make_list(ss, 1, cc);
				t->u.cont.k = cc;
				sly_value tmp = cps_gensym_temporary_name(ss);
				k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, make_list(ss, 1, tmp));
				cc = k->name;
				cps_graph_set(ss, graph, cc, k);
				if ((identifier_p(e) || symbol_p(e))
					&& !primop_p(e)) {
					proc = strip_syntax(e);
				} else {
					cc = cps_translate(ss, cc, graph, CAR(rest));
					k = cps_graph_ref(graph, cc);
					proc = tmp;
				}
				t->u.cont.expr->u.call.proc = proc;
				return cc;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "values", 6))) {
				sly_value vlist = SLY_NULL;
				sly_value e;
				t = cps_new_term();
				t->u.cont.k = cc;
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				sly_value tmp = cps_gensym_temporary_name(ss);
				k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, make_list(ss, 1, tmp));
				cc = k->name;
				cps_graph_set(ss, graph, cc, k);
				sly_value name;
				while (!null_p(rest)) {
					e = CAR(rest);
					if (identifier_p(e) || symbol_p(e)) {
						name = strip_syntax(e);
					} else { // constant or expression
						cc = cps_translate(ss, cc, graph, e);
						k = cps_graph_ref(graph, cc);
						name = tmp;
						tmp = car(k->u.kargs.vars);
					}
					vlist = list_append(ss, vlist, cons(ss, name, SLY_NULL));
					rest = CDR(rest);
				}
				t->u.cont.expr->type = tt_cps_values;
				t->u.cont.expr->u.values.args = vlist;
				return cc;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "call-with-values", 16))) {
				sly_value pe = CAR(rest);
				sly_value re = CAR(CDR(rest));
				sly_value kp;
				sly_value tmp_name = cps_gensym_temporary_name(ss);
				cc = cps_translate(ss, cc, graph,
								   make_list(ss, 3,
											 make_symbol(ss, "apply", 5),
											 re,
											 tmp_name));
				cc = cps_translate(ss, cc, graph, cons(ss, pe, SLY_NULL));
				kp = cc;
				k = cps_graph_ref(graph, kp);
				k = cps_graph_ref(graph, k->u.kargs.term->u.cont.k);
				k->u.kreceive.arity.req = SLY_NULL;
				k->u.kreceive.arity.rest = tmp_name;
				k = cps_graph_ref(graph, k->u.kreceive.k);
				k->u.kargs.vars = make_list(ss, 1, tmp_name);
				return cc;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "lambda", 6))) {
				sly_value arg_formals = CAR(rest);
				sly_value kp = cps_gensym_label_name(ss);
				cps_graph_set(ss, graph, kp, cps_make_ktail(ss, 1));
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_proc;
				t->u.cont.k = cc;
				{
					k = cps_make_ktail(ss, 0);
					k->type = tt_cps_kproc;
					k->name = cps_gensym_label_name(ss);
					k->u.kproc.tail = kp;
					form = cons(ss, make_symbol(ss, "begin", 5), CDR(rest));
					k->u.kproc.body = cps_translate(ss, kp, graph, form);
					sly_value req = SLY_NULL;
					while (syntax_pair_p(arg_formals) || pair_p(arg_formals)) {
						sly_value a = strip_syntax(CAR(arg_formals));
						req = list_append(ss, req,
										  cons(ss, a, SLY_NULL));
						arg_formals = CDR(arg_formals);
					}
					k->u.kproc.arity.rest = null_p(arg_formals) ?
						SLY_FALSE : strip_syntax(arg_formals);
					k->u.kproc.arity.req = req;
					t->u.cont.expr->u.proc.k = k->name;
					cps_graph_set(ss, graph, k->name, k);
				}
				{
					sly_value name = cps_gensym_label_name(ss);
					cc = name;
					k = cps_make_kargs(ss, name, t,
									   make_list(ss, 1, cps_gensym_temporary_name(ss)));
					cps_graph_set(ss, graph, cc, k);
				}
				return cc;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "begin", 5))) {
				if (!null_p(CDR(rest))) {
					cc = cps_translate(ss, cc, graph, cons(ss, fst, CDR(rest)));
				}
				return cps_translate(ss, cc, graph, CAR(rest));
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "if", 2))) {
				sly_value cform = CAR(rest);
				sly_value tform = CAR(CDR(rest));
				sly_value fform = CAR(CDR(CDR(rest)));
				sly_value kt, kf;
				kt = cps_translate(ss, cc, graph, tform);
				kf = cps_translate(ss, cc, graph, fform);
				t = cps_new_term();
				k = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
								   make_list(ss, 1, cps_gensym_temporary_name(ss)));
				t->type = tt_cps_branch;
				t->u.branch.arg  = car(k->u.kargs.vars);
				t->u.branch.kt = kt;
				t->u.branch.kf = kf;
				cc = k->name;
				cps_graph_set(ss, graph, cc, k);
				return cps_translate(ss, cc, graph, cform);
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "quote", 5))) {
				form = CAR(rest);
				CPS_CONSTANT();
				return cc;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "syntax-quote", 12))) {
				syntax_quote = 1;
				form = CAR(rest);
				CPS_CONSTANT();
				return cc;
			}
		}
		/* call */
		sly_value vlist = SLY_NULL;
		sly_value e;
		k = cps_make_ktail(ss, 0);
		k->type = tt_cps_kreceive;
		k->name = cps_gensym_label_name(ss);
		k->u.kreceive.k = cc;
		k->u.kreceive.arity.req = cons(ss,
									   cps_gensym_temporary_name(ss),
									   SLY_NULL);
		k->u.kreceive.arity.rest = SLY_FALSE;
		cc = k->name;
		cps_graph_set(ss, graph, cc, k);
		t = cps_new_term();
		t->u.cont.k = cc;
		t->type = tt_cps_continue;
		t->u.cont.expr = cps_new_expr();
		sly_value tmp = cps_gensym_temporary_name(ss);
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, make_list(ss, 1, tmp));
		cc = k->name;
		cps_graph_set(ss, graph, cc, k);
		while (!null_p(form)) {
			e = CAR(form);
			sly_value name;
			if (identifier_p(e) || symbol_p(e)) {
				name = strip_syntax(e);
			} else { // constant or expression
				cc = cps_translate(ss, cc, graph, e);
				k = cps_graph_ref(graph, cc);
				name = tmp;
				tmp = car(k->u.kargs.vars);
			}
			vlist = list_append(ss, vlist, cons(ss, name, SLY_NULL));
			form = CDR(form);
		}
		sly_value name = car(vlist);
		vlist = cdr(vlist);
		if (primop_p(name)) {
			t->u.cont.expr->type = tt_cps_primcall;
		} else {
			t->u.cont.expr->type = tt_cps_call;
		}
		t->u.cont.expr->u.call.args  = vlist;
		t->u.cont.expr->u.call.proc = name;
		return cc;
	} else if (identifier_p(form) || symbol_p(form)) {
		sly_value s = strip_syntax(form);
		t = cps_new_term();
		t->type = tt_cps_continue;
		t->u.cont.expr = cps_new_expr();
		if (primop_p(s)) {
			t->u.cont.expr->type = tt_cps_prim;
			t->u.cont.expr->u.prim.name = s;
		} else {
			t->u.cont.expr->type = tt_cps_values;
			t->u.cont.expr->u.values.args = s;
		}
		t->u.cont.k = cc;
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
						   make_list(ss, 1, cps_gensym_temporary_name(ss)));
		cc = k->name;
		cps_graph_set(ss, graph, cc, k);
		return cc;
	}
	CPS_CONSTANT();
	return cc;
}

static void _cps_display(Sly_State *ss, sly_value graph, sly_value visited, sly_value k);

static void
display_expr(Sly_State *ss, sly_value graph, sly_value visited, CPS_Expr *expr)
{
	switch (expr->type) {
	case tt_cps_call: {
		sly_display(cons(ss, cstr_to_symbol("call"),
						 cons(ss, expr->u.call.proc,
							  expr->u.call.args)), 1);
	} break;
	case tt_cps_values: {
		sly_display(cons(ss, cstr_to_symbol("values"),
						 expr->u.values.args), 1);
	} break;
	case tt_cps_const: {
		printf("(const ");
		sly_display(expr->u.constant.value, 1);
		printf(")");
	} break;
	case tt_cps_proc: {
		printf("(proc ");
		CPS_Kont *kont = cps_graph_ref(graph, expr->u.proc.k);
		sly_display(kont->name, 1);
		printf(");\n");
		_cps_display(ss, graph, visited, expr->u.proc.k);
		printf(";");
	} break;
	case tt_cps_prim: {
		printf("(prim ");
		sly_display(expr->u.prim.name, 1);
	} break;
	case tt_cps_primcall: {
		sly_display(cons(ss, cstr_to_symbol("primcall"),
						 cons(ss, expr->u.primcall.prim,
							  expr->u.primcall.args)), 1);
	} break;
	default: sly_assert(0, "Error not a cps expression");
	}
}

static void
display_term(Sly_State *ss, sly_value graph, sly_value visited, CPS_Term *term)
{
	switch (term->type) {
	case tt_cps_continue: {
		sly_value k = term->u.cont.k;
		CPS_Kont *kont = cps_graph_ref(graph, k);
		sly_display(kont->name, 1);
		printf(" ");
		display_expr(ss, graph, visited, term->u.cont.expr);
		printf(";\n");
		_cps_display(ss, graph, visited, term->u.cont.k);
	} break;
	case tt_cps_branch: {
		sly_value kt = term->u.branch.kt;
		sly_value kf = term->u.branch.kf;
		printf("if ");
		sly_display(term->u.branch.arg, 1);
		printf(" then ");
		CPS_Kont *k = cps_graph_ref(graph, kt);
		sly_display(k->name, 1);
		k = cps_graph_ref(graph, kf);
		printf(" else ");
		sly_display(k->name, 1);
		printf(";\n");
		_cps_display(ss, graph, visited, kt);
		_cps_display(ss, graph, visited, kf);
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
}

static void
_cps_display(Sly_State *ss, sly_value graph, sly_value visited, sly_value k)
{
begin:
	if (cps_graph_is_member(visited, k)) {
		return;
	}
	CPS_Kont *kont = cps_graph_ref(graph, k);
	cps_graph_set(ss, visited, k, kont);
	switch (kont->type) {
	case tt_cps_ktail: {
		return;
	} break;
	case tt_cps_kargs: {
		printf("let ");
		sly_display(kont->name, 1);
		printf(" = kargs ");
		sly_display(kont->u.kargs.vars, 1);
		printf(" -> ");
		display_term(ss, graph, visited, kont->u.kargs.term);
		return;
	} break;
	case tt_cps_kreceive: {
		printf("let ");
		sly_display(kont->name, 1);
		u32 req = list_len(kont->u.kreceive.arity.req);
		char *rst = kont->u.kreceive.arity.rest == SLY_FALSE ? "#f" : "#t";
		printf(" = kreceive (req: %u, rest: %s) => ", req, rst);
		CPS_Kont *tmp = cps_graph_ref(graph, kont->u.kreceive.k);
		sly_display(tmp->name, 1);
		printf(";\n");
		cps_graph_set(ss, visited, k, kont);
		k = kont->u.kreceive.k;
		goto begin;
	} break;
	case tt_cps_kproc: {
		printf("let ");
		sly_display(kont->name, 1);
		printf(" = λ ");
		sly_value arity = kont->u.kproc.arity.req;
		if (kont->u.kproc.arity.rest != SLY_FALSE) {
			if (null_p(arity)) {
				arity = kont->u.kproc.arity.rest;
			} else {
				sly_value a = arity;
				while (!null_p(cdr(a))) {
					a = cdr(a);
				}
				set_cdr(a, kont->u.kproc.arity.rest);
			}
		}
		sly_display(arity, 1);
		printf(" -> ");
		CPS_Kont *tmp = cps_graph_ref(graph, kont->u.kproc.body);
		sly_display(tmp->name, 1);
		printf(" : ");
		tmp = cps_graph_ref(graph, kont->u.kproc.tail);
		sly_display(tmp->name, 1);
		cps_graph_set(ss, visited, k, kont);
		k = kont->u.kproc.body;
		printf(";\n");
		goto begin;
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
}

void
cps_display(Sly_State *ss, sly_value graph, sly_value k)
{
	_cps_display(ss, graph, make_dictionary(ss), k);
}

static char *
next_arg(int *argc, char **argv[])
{
	if (argc == 0) {
		return NULL;
	}
	char *arg = **argv;
	(*argc)--;
	(*argv)++;
	return arg;
}

int
main(int argc, char *argv[])
{
	char *arg = next_arg(&argc, &argv);
	int debug_info = 0;
	if (argc) {
		while (argc) {
			arg = next_arg(&argc, &argv);
			if (strcmp(arg, "-I") == 0) {
				debug_info = 1;
			} else if (strcmp(arg, "--expand") == 0) {
				Sly_State ss = {0};
				sly_value ast = sly_expand_only(&ss, next_arg(&argc, &argv));
				sly_value graph = make_dictionary(&ss);
				sly_value name = make_symbol(&ss, "$tkexit", 7);
				CPS_Kont *kont = cps_make_ktail(&ss, 0);
				kont->name = name;
				cps_graph_set(&ss, graph, name, kont);
				sly_displayln(strip_syntax(ast));
				cps_init_primops(&ss);
				sly_value entry = cps_translate(&ss, name, graph, ast);
				cps_display(&ss, graph, entry);
			} else {
				printf("Running file %s ...\n", arg);
				sly_do_file(arg, debug_info);
				printf("\n");
			}
		}
	} else {
		printf("No source file provided.\nExiting ...\n");
	}
	return 0;
}
