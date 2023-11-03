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

/*
 * CPS intermediate language
 * TODO: Move this to seperate file.
 */

/*
 * Referances:
 * How Guile does it
 * - https://www.gnu.org/software/guile/manual/html_node/CPS-in-Guile.html
 * Blog from a guile dev
 * - https://wingolog.org/
 * Good book
 * - https://www.amazon.com/Compiling-Continuations-Andrew-W-Appel/dp/052103311X
 */

#define CAR(val) (syntax_p(val) ? car(syntax_to_datum(val)) : car(val))
#define CDR(val) (syntax_p(val) ? cdr(syntax_to_datum(val)) : cdr(val))

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
	tt_prim_null = 0,
	tt_prim_add,
	tt_prim_sub,
	tt_prim_mul,
	tt_prim_div,
	tt_prim_idiv,
	tt_prim_mod,
	tt_prim_bw_and,
	tt_prim_bw_ior,
	tt_prim_bw_xor,
	tt_prim_bw_eqv,
	tt_prim_bw_nor,
	tt_prim_bw_nand,
	tt_prim_bw_not,
	tt_prim_bw_shift,
	tt_prim_void,
	tt_prim_apply,
	tt_prim_cons,
	tt_prim_car,
	tt_prim_cdr,
	tt_prim_list,
};

typedef sly_value (*fn_primop)(Sly_State *ss, sly_value arg_list);

struct primop {
	char *cstr;
	sly_value name;
	fn_primop fn;
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

typedef struct _var_info {
	int used;
	int escapes;
	int isalias;
	int which;
	CPS_Expr *binding;
	struct _var_info *alt;
} CPS_Var_Info;

CPS_Kont *cps_graph_ref(sly_value graph, sly_value k);
void cps_graph_set(Sly_State *ss, sly_value graph, sly_value k, CPS_Kont *kont);
CPS_Expr *cps_binding_ref(sly_value bindings, sly_value v);
void cps_binding_update(Sly_State *ss, sly_value bindings, sly_value v, CPS_Expr *e);
int cps_graph_is_member(sly_value graph, sly_value k);
CPS_Term *cps_new_term(void);
CPS_Expr *cps_make_constant(sly_value value);
CPS_Expr *cps_new_expr(void);
CPS_Var_Info *cps_new_var_info(CPS_Expr *binding, int isalias, int which);
sly_value cps_gensym_temporary_name(Sly_State *ss);
sly_value cps_gensym_label_name(Sly_State *ss);
CPS_Kont *cps_make_kargs(Sly_State *ss, sly_value name, CPS_Term *term, sly_value vars);
CPS_Kont *cps_make_ktail(Sly_State *ss, int genname);
sly_value cps_opt_contraction_phase(Sly_State *ss, sly_value graph, sly_value k, int debug);
void cps_init_primops(Sly_State *ss);
sly_value cps_translate(Sly_State *ss, sly_value cc, sly_value graph, sly_value form);
void cps_display(Sly_State *ss, sly_value graph, sly_value k);
void cps_display_var_info(Sly_State *ss, sly_value graph, sly_value var_info);

static sly_value prim_add(Sly_State *ss, sly_value arg_list);
static sly_value prim_sub(Sly_State *ss, sly_value arg_list);
static sly_value prim_mul(Sly_State *ss, sly_value arg_list);
static sly_value prim_div(Sly_State *ss, sly_value arg_list);
static sly_value prim_idiv(Sly_State *ss, sly_value arg_list);
static sly_value prim_mod(Sly_State *ss, sly_value arg_list);
static sly_value prim_cons(Sly_State *ss, sly_value arg_list);
static sly_value prim_car(Sly_State *ss, sly_value arg_list);
static sly_value prim_cdr(Sly_State *ss, sly_value arg_list);
static sly_value prim_list(Sly_State *ss, sly_value arg_list);

static struct primop primops[] = {
	[tt_prim_null]		= {NULL, SLY_VOID, NULL},
	[tt_prim_add]		= {"+", SLY_VOID, prim_add},    // (+ . args)   ; addition
	[tt_prim_sub]		= {"-", SLY_VOID, prim_sub},    // (- . args)   ; subtraction
	[tt_prim_mul]		= {"*", SLY_VOID, prim_mul},    // (* . args)   ; multiplication
	[tt_prim_div]		= {"/", SLY_VOID, prim_div},    // (/ . args)   ; real division
	[tt_prim_idiv]		= {"div", SLY_VOID, prim_idiv}, // (div . args) ; integer division
	[tt_prim_mod]		= {"%", SLY_VOID, prim_mod},    // (% x y)      ; modulo
	[tt_prim_bw_and]	= {"bitwise-and"},              // (bitwise-and x y)
	[tt_prim_bw_ior]	= {"bitwise-ior"},              // (bitwise-ior x y)
	[tt_prim_bw_xor]	= {"bitwise-xor"},              // (bitwise-xor x y)
	[tt_prim_bw_eqv]	= {"bitwise-eqv"},              // (bitwise-eqv x y)
	[tt_prim_bw_nor]	= {"bitwise-nor"},              // (bitwise-nor x y)
	[tt_prim_bw_nand]	= {"bitwise-nand"},             // (bitwise-nand x y)
	[tt_prim_bw_not]	= {"bitwise-not"},              // (bitwise-not x y)
	[tt_prim_bw_shift]  = {"arithmetic-shift"},         // (bitwise-shift x y)
	[tt_prim_void]		= {"void"},                     // (void) ; #<void>
	[tt_prim_apply]	    = {"apply"},
	[tt_prim_cons]      = {"cons", SLY_VOID, prim_cons},
	[tt_prim_car]       = {"car", SLY_VOID, prim_car},
	[tt_prim_cdr]       = {"cdr", SLY_VOID, prim_cdr},
	[tt_prim_list]      = {"list", SLY_VOID, prim_list},
};

static sly_value
prim_add(Sly_State *ss, sly_value arg_list)
{
	sly_value total = make_int(ss, 0);
	while (!null_p(arg_list)) {
		total = sly_add(ss, total, car(arg_list));
		arg_list = cdr(arg_list);
	}
	return total;
}

static sly_value
prim_sub(Sly_State *ss, sly_value arg_list)
{
	size_t nargs = list_len(arg_list);
	sly_assert(nargs >= 1, "Error subtraction requires at least 1 argument");
	if (nargs == 1) {
		return sly_sub(ss, make_int(ss, 0), car(arg_list));
	}
	sly_value fst = car(arg_list);
	sly_value total = make_int(ss, 0);
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		total = sly_add(ss, total, car(arg_list));
		arg_list = cdr(arg_list);
	}
	return sly_sub(ss, fst, total);
}

static sly_value
prim_mul(Sly_State *ss, sly_value arg_list)
{
	sly_value total = make_int(ss, 1);
	while (!null_p(arg_list)) {
		total = sly_mul(ss, total, car(arg_list));
		arg_list = cdr(arg_list);
	}
	return total;
}

static sly_value
prim_div(Sly_State *ss, sly_value arg_list)
{
	size_t nargs = list_len(arg_list);
	sly_assert(nargs >= 1, "Error division requires at least 1 argument");
	if (nargs == 1) {
		return sly_div(ss, make_int(ss, 1), car(arg_list));
	}
	sly_value fst = car(arg_list);
	sly_value total = make_int(ss, 1);
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		total = sly_mul(ss, total, car(arg_list));
		arg_list = cdr(arg_list);
	}
	return sly_div(ss, fst, total);
}

static sly_value
prim_idiv(Sly_State *ss, sly_value arg_list)
{
	size_t nargs = list_len(arg_list);
	sly_assert(nargs >= 1, "Error division requires at least 1 argument");
	if (nargs == 1) {
		return sly_floor_div(ss, make_int(ss, 1), car(arg_list));
	}
	sly_value fst = car(arg_list);
	sly_value total = make_int(ss, 1);
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		total = sly_mul(ss, total, car(arg_list));
		arg_list = cdr(arg_list);
	}
	return sly_floor_div(ss, fst, total);
}

static sly_value
prim_mod(Sly_State *ss, sly_value arg_list)
{
	size_t nargs = list_len(arg_list);
	sly_assert(nargs == 2, "Error modulo requires 2 arguments");
	sly_value x = car(arg_list);
	sly_value y = car(cdr(arg_list));
	return sly_mod(ss, x, y);
}

static sly_value
prim_cons(Sly_State *ss, sly_value arg_list)
{
	sly_assert(list_len(arg_list) == 2, "Error cons requires 2 arguments");
	return cons(ss, car(arg_list), car(cdr(arg_list)));
}

static sly_value
prim_car(Sly_State *ss, sly_value arg_list)
{
	UNUSED(ss);
	sly_assert(list_len(arg_list) == 1, "Error car requires 2 arguments");
	return car(car(arg_list));
}

static sly_value
prim_cdr(Sly_State *ss, sly_value arg_list)
{
	UNUSED(ss);
	sly_assert(list_len(arg_list) == 1, "Error cdr requires 2 arguments");
	return cdr(car(arg_list));
}

static sly_value
prim_list(Sly_State *ss, sly_value arg_list)
{
	UNUSED(ss);
	return arg_list;
}

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
	if (x == SLY_FALSE) {
		printf("\n\nk = ");
		sly_displayln(k);
		sly_assert(0, "Error label not set");
	}
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

CPS_Var_Info *
cps_new_var_info(CPS_Expr *binding, int isalias, int which)
{
	CPS_Var_Info *vi = GC_MALLOC(sizeof(*vi));
	vi->used = 0;
	vi->escapes = 0;
	vi->binding = binding;
	vi->isalias = isalias;
	vi->which = which;
	vi->alt = NULL;
	return vi;
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

static CPS_Kont *
cps_copy_kont(CPS_Kont *k)
{
	CPS_Kont *j = GC_MALLOC(sizeof(*k));
	memcpy(j, k, sizeof(*k));
	return j;
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
//
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
				sly_value args = req;
				if (null_p(arg_formals)) {
					k->u.kproc.arity.rest = SLY_FALSE;
				} else {
					sly_value tmp = strip_syntax(arg_formals);
					k->u.kproc.arity.rest = tmp;
					args = list_append(ss, args, cons(ss, tmp, SLY_NULL));
				}
				{ // bind function parameters to the body continuation
					CPS_Kont *kont = cps_graph_ref(graph, k->u.kproc.body);
					sly_assert(kont->type == tt_cps_kargs, "Error expected kargs");
					kont->u.kargs.vars = args;
				}
				k->u.kproc.arity.req = req;
				t->u.cont.expr->u.proc.k = k->name;
				cps_graph_set(ss, graph, k->name, k);
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
		sly_value vars = make_list(ss, 1, cps_gensym_temporary_name(ss));
		t = cps_new_term();
		t->type = tt_cps_continue;
		t->u.cont.expr = cps_new_expr();
		t->u.cont.expr->type = tt_cps_values;
		t->u.cont.expr->u.values.args = vars;
		t->u.cont.k = cc;
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, vars);
		k->name = cps_gensym_label_name(ss);
		cps_graph_set(ss, graph, k->name, k);
		cc = k->name;
		k = cps_make_ktail(ss, 0);
		k->type = tt_cps_kreceive;
		k->name = cps_gensym_label_name(ss);
		k->u.kreceive.k = cc;
		k->u.kreceive.arity.req = vars;
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
			t->u.cont.expr->u.values.args = make_list(ss, 1, s);
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

static sly_value
cps_get_const(sly_value var_info, sly_value var)
{
	sly_value s = dictionary_ref(var_info, var, SLY_VOID);
	if (!void_p(s)) {
		CPS_Var_Info *vi = GET_PTR(s);
		if (vi->binding
			&& vi->alt == NULL
			&& vi->binding->type == tt_cps_const) {
			return vi->binding->u.constant.value;
		}
	}
	return SLY_VOID;
}

static sly_value
cps_get_const_number(sly_value var_info, sly_value var)
{
	sly_value val = cps_get_const(var_info, var);
	if (void_p(val)
		|| !number_p(val)) {
		return SLY_FALSE;
	}
	return val;
}

static void
cps_var_def_info(Sly_State *ss, sly_value tbl, sly_value name, CPS_Expr *expr, int isalias, int which)
{
	CPS_Var_Info *vi = cps_new_var_info(expr, isalias, which);
	dictionary_set(ss, tbl, name, (sly_value)vi);
}

static void
cps_var_info_inc_used(sly_value tbl, sly_value name)
{
	sly_value s = dictionary_ref(tbl, name, SLY_VOID);
	if (!void_p(s)) {
		((CPS_Var_Info *)s)->used++;
	}
}

static void
cps_var_info_inc_escapes(sly_value tbl, sly_value name)
{
	sly_value s = dictionary_ref(tbl, name, SLY_VOID);
	if (!void_p(s)) {
		((CPS_Var_Info *)s)->escapes++;
	}
}

static void
cps_var_info_visit_expr(sly_value tbl, CPS_Expr *expr)
{
	sly_value args;
	switch (expr->type) {
	case tt_cps_primcall: {
		args = expr->u.primcall.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			cps_var_info_inc_used(tbl, var);
			args = cdr(args);
		}
	} break;
	case tt_cps_call: {
		sly_value proc = expr->u.call.proc;
		cps_var_info_inc_used(tbl, proc);
		args = expr->u.call.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			cps_var_info_inc_used(tbl, var);
			cps_var_info_inc_escapes(tbl, var);
			args = cdr(args);
		}
	} break;
	case tt_cps_kcall: {
		args = expr->u.kcall.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			cps_var_info_inc_used(tbl, var);
			args = cdr(args);
		}
	} break;
	case tt_cps_values: {
		args = expr->u.values.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			cps_var_info_inc_used(tbl, var);
			args = cdr(args);
		}

	}
	}
}

static CPS_Var_Info *
var_info_cat(CPS_Var_Info *info1, CPS_Var_Info *info2)
{
	if (info1 == NULL) return info2;
	if (info2 == NULL) return info1;
	if (info1 == info2) return info1;
	info1->alt = var_info_cat(info1->alt, info2);
	return info1;
}

static sly_value
cps_var_tbl_union(Sly_State *ss, sly_value t1, sly_value t2)
{
	if (void_p(t1)) return t2;
	if (void_p(t2)) return t1;
	sly_assert(dictionary_p(t1), "Type Error expected dictionary");
	sly_assert(dictionary_p(t2), "Type Error expected dictionary");
	sly_value nt = copy_dictionary(ss, t1);
	vector *vec = GET_PTR(t2);
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value entry = vec->elems[i];
		if (!slot_is_free(entry)) {
			sly_value name = car(entry);
			sly_value info2 = cdr(entry);
			sly_value info1 = dictionary_ref(nt, name, SLY_VOID);
			if (info1 != info2) {
				info1 = (sly_value)var_info_cat(GET_PTR(info1), GET_PTR(info2));
				dictionary_set(ss, nt, name, info1);
			}
		}
	}
	return nt;
}

static sly_value
cps_var_info_propagate(Sly_State *ss, sly_value state, sly_value out)
{
	sly_assert(dictionary_p(out), "Type Error expected dictionary");
	sly_value nstate = copy_dictionary(ss, state);
	vector *vout = GET_PTR(out);
	for (size_t i = 0; i < vout->cap; ++i) {
		sly_value entry = vout->elems[i];
		if (!slot_is_free(entry)) {
			sly_value name = car(entry);
			sly_value ot = cdr(entry);
			sly_value nt = dictionary_ref(nstate, name, SLY_VOID);
			dictionary_set(ss, nstate, name, cps_var_tbl_union(ss, nt, ot));
		}
	}
	return nstate;
}

static sly_value
cps_collect_var_info(Sly_State *ss, sly_value graph, sly_value state,
					 sly_value prev_tbl, CPS_Expr *expr, sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_kargs: {
		sly_value vlist = kont->u.kargs.vars;
		sly_value var_tbl = copy_dictionary(ss, prev_tbl);
		int isalias = expr && (expr->type == tt_cps_values);
		int which = 0;
		CPS_Term *term = kont->u.kargs.term;
		while (!null_p(vlist)) {
			cps_var_def_info(ss, var_tbl, car(vlist), expr, isalias, which);
			which++;
			vlist = cdr(vlist);
		}
		dictionary_set(ss, state, kont->name, var_tbl);
		switch (term->type) {
		case tt_cps_continue: {
			CPS_Expr *nexpr = term->u.cont.expr;
			if (nexpr->type == tt_cps_proc) {
				state = cps_collect_var_info(ss, graph, state, var_tbl, nexpr,
											 nexpr->u.proc.k);
			} else {
				cps_var_info_visit_expr(var_tbl, nexpr);
			}
			state = cps_collect_var_info(ss, graph, state, var_tbl, nexpr,
										 term->u.cont.k);
		} break;
		case tt_cps_branch: {
			cps_var_info_inc_used(var_tbl, term->u.branch.arg);
			sly_value b1, b2;
			b1 = copy_dictionary(ss, state);
			b2 = copy_dictionary(ss, state);
			b1 = cps_collect_var_info(ss, graph, b1, var_tbl, NULL, term->u.branch.kt);
			b2 = cps_collect_var_info(ss, graph, b2, var_tbl, NULL, term->u.branch.kf);
			state = cps_var_info_propagate(ss, state,
										   cps_var_info_propagate(ss, b1, b2));
		} break;
		}
	} break;
	case tt_cps_kreceive: {
		printf("INFO: kreceive reached\n");
		state = cps_collect_var_info(ss, graph, state, prev_tbl, expr, kont->u.kreceive.k);
	} break;
	case tt_cps_kproc: {
		sly_value req = kont->u.kproc.arity.req;
		sly_value rest = kont->u.kproc.arity.rest;
		sly_value var_tbl = copy_dictionary(ss, prev_tbl);
		while (!null_p(req)) {
			cps_var_def_info(ss, var_tbl, car(req), NULL, 0, 0);
			req = cdr(req);
		}
		if (rest != SLY_FALSE) {
			cps_var_def_info(ss, var_tbl, rest, NULL, 0, 0);
		}
		state = cps_collect_var_info(ss, graph, state, var_tbl, NULL, kont->u.kproc.body);
	} break;
	case tt_cps_ktail: {
		printf("INFO: tail reached\n");
	} break;
	default: {
		sly_assert(0, "Error Invalid continuation");
	} break;
	}
	return state;
}

static int DELTA = 0;

static int
var_is_dead(sly_value var_info, sly_value var)
{
	if (void_p(var_info)) {
		return 0;
	}
	sly_value s = dictionary_ref(var_info, var, SLY_VOID);
	if (void_p(s)) {
		return 0;  // No info. Variable may be imported
	}
	CPS_Var_Info *vi = GET_PTR(s);
	int used = 0;
	int escapes = 0;
	while (vi) {
		used += vi->used;
		escapes += vi->escapes;
		vi = vi->alt;
	}
	return (used + escapes) == 0;
}

static int
var_is_used_once(sly_value var_info, sly_value var)
{
	sly_value s = dictionary_ref(var_info, var, SLY_VOID);
	if (void_p(s)) {
		return 0;  // No info. Variable may be imported
	}
	CPS_Var_Info *vi = GET_PTR(s);
	if (vi->alt || vi->escapes) {
		return 0;
	}
	return vi->used == 1;
}

static CPS_Expr *
cps_opt_constant_folding_visit_expr(Sly_State *ss, sly_value var_info, CPS_Expr *expr)
{
	sly_value args = expr->u.primcall.args;
	sly_value val_list = SLY_NULL;
	if (expr->type == tt_cps_primcall) {
		enum prim op = primop_p(expr->u.primcall.prim);
		switch (op) {
		case tt_prim_null: break;
		case tt_prim_add:
		case tt_prim_sub:
		case tt_prim_mul:
		case tt_prim_div:
		case tt_prim_idiv: {
			while (!null_p(args)) {
				sly_value val = cps_get_const_number(var_info, car(args));
				if (val == SLY_FALSE) {
					return expr;
				}
				val_list = list_append(ss, val_list,
									   cons(ss, val, SLY_NULL));
				args = cdr(args);
			}
		} break;
		case tt_prim_mod: {
			sly_assert(list_len(args) == 2, "Error arity mismatch");
			while (!null_p(args)) {
				sly_value val = cps_get_const_number(var_info, car(args));
				if (val == SLY_FALSE || !int_p(val)) {
					return expr;
				}
				val_list = list_append(ss, val_list,
									   cons(ss, val, SLY_NULL));
				args = cdr(args);
			}
		} break;
		case tt_prim_bw_and: break;
		case tt_prim_bw_ior: break;
		case tt_prim_bw_xor: break;
		case tt_prim_bw_eqv: break;
		case tt_prim_bw_nor: break;
		case tt_prim_bw_nand: break;
		case tt_prim_bw_not: break;
		case tt_prim_bw_shift: break;
		case tt_prim_void: break;
		case tt_prim_apply: break;
		case tt_prim_cons: {
			sly_assert(list_len(args) == 2, "Error arity mismatch");
			sly_value v1 = cps_get_const(var_info, car(args));
			if (v1 == SLY_VOID) {
				return expr;
			}
			sly_value v2 = cps_get_const(var_info, car(cdr(args)));
			if (v1 == SLY_VOID) {
				return expr;
			}
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = cons(ss, v1, v2);
			return expr;
		} break;
		case tt_prim_car: {
			sly_assert(list_len(args) == 1, "Error arity mismatch");
			sly_value v = cps_get_const(var_info, car(args));
			if (!pair_p(v)) {
				return expr;
			}
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = car(v);
			return expr;
		} break;
		case tt_prim_cdr: {
			sly_assert(list_len(args) == 1, "Error arity mismatch");
			sly_value v = cps_get_const(var_info, car(args));
			if (!pair_p(v)) {
				return expr;
			}
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = cdr(v);
			return expr;
		} break;
		case tt_prim_list: {
			while (!null_p(args)) {
				sly_value val = cps_get_const(var_info, car(args));
				if (void_p(val)) {
					return expr;
				}
				val_list = list_append(ss, val_list,
									   cons(ss, val, SLY_NULL));
				args = cdr(args);
			}
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = val_list;
			return expr;
		} break;
		}
		if (primops[op].fn) {
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = primops[op].fn(ss, val_list);
		}
	}
	return expr;
}

static int
expr_has_no_possible_side_effect(CPS_Expr *expr)
{
	// expression types that do do not have side effects
	// can be safely removed if the result is unused
	return expr->type == tt_cps_const
		|| expr->type == tt_cps_proc
		|| expr->type == tt_cps_prim
		|| expr->type == tt_cps_values;
}

static sly_value
cps_opt_constant_folding(Sly_State *ss, sly_value graph, sly_value var_info, sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	sly_value new_graph = make_dictionary(ss);
	cps_graph_set(ss, new_graph, k, kont);
	switch (kont->type) {
	case tt_cps_kargs: {
		sly_value info = dictionary_ref(var_info, k, SLY_VOID);
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			sly_assert(!void_p(info), "Error No var info");
			CPS_Expr *expr = cps_opt_constant_folding_visit_expr(ss, info, term->u.cont.expr);
			CPS_Kont *new_kont = cps_copy_kont(kont);
			sly_value nk = term->u.cont.k;
			if (expr != term->u.cont.expr) {
				new_kont->u.kargs.term = cps_new_term();
				new_kont->u.kargs.term->u.cont.expr = expr;
				CPS_Kont *receive = cps_graph_ref(graph, nk);
				if (receive->type == tt_cps_kreceive) { // expression folded, don't need receive
					new_kont->u.kargs.term->u.cont.k = receive->u.kreceive.k;
				} else {
					new_kont->u.kargs.term->u.cont.k = nk;
				}
				DELTA++;
			}
			if (expr->type == tt_cps_proc) {
				new_graph = dictionary_union(ss, new_graph,
											 cps_opt_constant_folding(ss, graph, var_info, expr->u.proc.k));
			}
			dictionary_set(ss, new_graph, k, (sly_value)new_kont);
			nk = new_kont->u.kargs.term->u.cont.k;
			CPS_Kont *next = cps_graph_ref(graph, nk);
			if (expr_has_no_possible_side_effect(expr)
				&& next->type == tt_cps_kargs
				&& list_len(next->u.kargs.vars) == 1) {
				info = dictionary_ref(var_info, nk, SLY_VOID);
				if (var_is_dead(info, car(next->u.kargs.vars))) {
					new_kont->u.kargs.term = next->u.kargs.term;
					term = new_kont->u.kargs.term;
					DELTA++;
				}
			}
			if (expr->type == tt_cps_values
				&& next->type == tt_cps_kargs
				&& list_len(next->u.kargs.vars) == 1
				&& var_is_used_once(info, car(expr->u.values.args))) {
				CPS_Var_Info *vi = GET_PTR(dictionary_ref(info, car(expr->u.values.args), SLY_VOID));
				if (expr_has_no_possible_side_effect(vi->binding)) {
					*expr = *vi->binding;
					DELTA++;
				}
			}
			return dictionary_union(ss, new_graph,
									cps_opt_constant_folding(ss, graph, var_info, term->u.cont.k));
		} else if (term->type == tt_cps_branch) {
			sly_value arg = cps_get_const(info, term->u.branch.arg);
			if (void_p(arg)) {
				new_graph = dictionary_union(ss, new_graph,
											 cps_opt_constant_folding(ss, graph, var_info, term->u.branch.kt));
				return dictionary_union(ss, new_graph,
										cps_opt_constant_folding(ss, graph, var_info, term->u.branch.kf));
			}
			CPS_Kont *new_kont = cps_copy_kont(kont);
			dictionary_set(ss, new_graph, k, (sly_value)new_kont);
			sly_value vars = new_kont->u.kargs.vars;
			new_kont->u.kargs.term = cps_new_term();
			new_kont->u.kargs.term->type = tt_cps_continue;
			CPS_Expr *expr = cps_new_expr();
			new_kont->u.kargs.term->u.cont.expr = expr;
			expr->type = tt_cps_values;
			expr->u.values.args = vars;
			if (arg == SLY_FALSE) {
				printf("HERE\n");
				new_kont->u.kargs.term->u.cont.k = term->u.branch.kf;
				return dictionary_union(ss, new_graph,
										cps_opt_constant_folding(ss, graph, var_info, term->u.branch.kf));
			} else {
				new_kont->u.kargs.term->u.cont.k = term->u.branch.kt;
				return dictionary_union(ss, new_graph,
										cps_opt_constant_folding(ss, graph, var_info, term->u.branch.kt));
			}
		}
		sly_assert(0, "Error Unreachable");
	} break;
	case tt_cps_kreceive: {
		return dictionary_union(ss, new_graph,
								cps_opt_constant_folding(ss, graph, var_info, kont->u.kreceive.k));
	} break;
	case tt_cps_kproc: {
		return dictionary_union(ss, new_graph,
								cps_opt_constant_folding(ss, graph, var_info, kont->u.kproc.body));
	} break;
	}
	return new_graph;
}

static sly_value
replace_ktails(Sly_State *ss, sly_value graph, sly_value new_graph,
			   sly_value ktail, sly_value kreplace, sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	CPS_Kont *new_kont = cps_copy_kont(kont);
	dictionary_set(ss, new_graph, k, (sly_value)new_kont);
	switch (kont->type) {
	case tt_cps_kargs: {
		new_kont->u.kargs.term = cps_new_term();
		*new_kont->u.kargs.term = *kont->u.kargs.term;
		if (kont->u.kargs.term->type == tt_cps_continue) {
			new_kont->u.kargs.term->u.cont.k =
				replace_ktails(ss, graph, new_graph,
							   ktail, kreplace, kont->u.kargs.term->u.cont.k);
		} else if (kont->u.kargs.term->type == tt_cps_branch) {
			new_kont->u.kargs.term->u.branch.kt =
				replace_ktails(ss, graph, new_graph,
							   ktail, kreplace, kont->u.kargs.term->u.branch.kt);
			new_kont->u.kargs.term->u.branch.kf =
				replace_ktails(ss, graph, new_graph,
							   ktail, kreplace, kont->u.kargs.term->u.branch.kf);
		}
	} break;
	case tt_cps_kreceive: {
		new_kont->u.kreceive.k = replace_ktails(ss, graph, new_graph,
												ktail, kreplace, kont->u.kreceive.k);
	} break;
	case tt_cps_kproc: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_ktail: {
		return kreplace;
	} break;
	}
	return k;
}

static CPS_Kont *
bind_args_to_k(Sly_State *ss, sly_value graph, CPS_Kont *kont,
			   sly_value cc, sly_value args, struct arity_t arity)
{
	sly_assert(list_len(args) >= list_len(arity.req), "Error arity mismatch");
	dictionary_set(ss, graph, kont->name, (sly_value)kont);
	if (list_len(arity.req) == 0) { // handle rest arg
		if (arity.rest == SLY_FALSE) {
			kont->u.kargs.term->u.cont.k = cc;
			kont->u.kargs.term->u.cont.expr = NULL;
			return kont;
		} else if (list_len(args) == 0) {
			// pass (const '()) => rest
			sly_value var = arity.rest;
			CPS_Expr *expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = SLY_NULL;
			CPS_Term *t = cps_new_term();
			t->type = tt_cps_continue;
			t->u.cont.k = cc;
			CPS_Kont *next = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
											make_list(ss, 1, var));
			kont->u.kargs.term->u.cont.k = next->name;
			kont->u.kargs.term->u.cont.expr = expr;
			return next;
		} else {
			// pass (primop list remaining args) => rest
			sly_value var = arity.rest;
			CPS_Expr *expr = cps_new_expr();
			expr->type = tt_cps_primcall;
			expr->u.primcall.prim = primops[tt_prim_list].name;
			expr->u.primcall.args = args;
			CPS_Term *t = cps_new_term();
			t->type = tt_cps_continue;
			t->u.cont.k = cc;
			CPS_Kont *next = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
											make_list(ss, 1, var));
			kont->u.kargs.term->u.cont.k = next->name;
			kont->u.kargs.term->u.cont.expr = expr;
			return next;
		}
	} else {
		// pass (car args) => kargs ((car arity)) -> ...
		// kont->u.kargs
		sly_value val = car(args);
		args = cdr(args);
		sly_value var = car(arity.req);
		arity.req = cdr(arity.req);
		CPS_Expr *expr = cps_new_expr();
		expr->type = tt_cps_values;
		expr->u.values.args = make_list(ss, 1, val);
		CPS_Term *t = cps_new_term();
		t->type = tt_cps_continue;
		CPS_Kont *next = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
										make_list(ss, 1, var));
		kont->u.kargs.term->u.cont.k = next->name;
		kont->u.kargs.term->u.cont.expr = expr;
		return bind_args_to_k(ss, graph, next, cc, args, arity);
	}
	sly_assert(0, "Unreachable");
}

static sly_value
cps_opt_beta_contraction(Sly_State *ss, sly_value graph, sly_value var_info, sly_value k)
{
	// Functions called only once can be inlined (beta-reduction)
	// without growing program size.
	CPS_Kont *kont = cps_graph_ref(graph, k);
	sly_value new_graph = make_dictionary(ss);
	cps_graph_set(ss, new_graph, k, kont);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			CPS_Expr *expr = term->u.cont.expr;
			if (expr->type == tt_cps_proc) {
				new_graph = dictionary_union(ss, new_graph,
											 cps_opt_constant_folding(ss, graph, var_info, expr->u.proc.k));
			}
			if (expr->type != tt_cps_call) {
				return dictionary_union(ss, new_graph,
										cps_opt_beta_contraction(ss, graph, var_info, term->u.cont.k));
			}
			sly_value info = dictionary_ref(var_info, k, SLY_VOID);
			if (!var_is_used_once(info, expr->u.call.proc)) {
				return dictionary_union(ss, new_graph,
										cps_opt_beta_contraction(ss, graph, var_info, term->u.cont.k));
			}
			DELTA++;
			CPS_Var_Info *vi = GET_PTR(dictionary_ref(info, expr->u.call.proc, SLY_VOID));
			CPS_Kont *kproc = cps_graph_ref(graph, vi->binding->u.proc.k);
			CPS_Kont *new_kont = cps_copy_kont(kont);
			sly_value body = kproc->u.kproc.body;
			sly_value args = expr->u.call.args;
			struct arity_t arity = kproc->u.kproc.arity;
			new_kont->u.kargs.term = cps_new_term();
			new_kont->u.kargs.term->type = tt_cps_continue;
			sly_value kk = term->u.cont.k;
			{
				CPS_Kont *krec = cps_graph_ref(graph, kk);
				if (krec->type == tt_cps_kreceive) {
					kk = krec->u.kreceive.k;
				}
			}
			new_kont->u.kargs.term->u.cont.expr = term->u.cont.expr;
			new_kont->u.kargs.term->u.cont.k = kk;
			dictionary_set(ss, new_graph, k, (sly_value)new_kont);
			CPS_Kont *kbody = cps_graph_ref(graph, body);
			sly_assert(kbody->type == tt_cps_kargs, "Error expected kargs");
			kbody->u.kargs.vars = SLY_NULL;
			CPS_Kont *begin = bind_args_to_k(ss, new_graph, new_kont, body, args, arity);
			if (begin->u.kargs.term->u.cont.expr == NULL) {
				begin->u.kargs.term = kbody->u.kargs.term;
			}
			replace_ktails(ss, graph, new_graph,
						   kproc->u.kproc.tail, kk, kproc->u.kproc.body);
			return dictionary_union(ss, new_graph,
									cps_opt_beta_contraction(ss, graph, var_info, kk));
		} else if (term->type == tt_cps_branch) {
			new_graph = dictionary_union(ss, new_graph,
										 cps_opt_beta_contraction(ss, graph, var_info, term->u.branch.kt));
			return dictionary_union(ss, new_graph,
									cps_opt_beta_contraction(ss, graph, var_info, term->u.branch.kf));
		}
		sly_assert(0, "Error Unreachable");
	} break;
	case tt_cps_kreceive: {
		return dictionary_union(ss, new_graph,
								cps_opt_beta_contraction(ss, graph, var_info, kont->u.kreceive.k));
	} break;
	case tt_cps_kproc: {
		return dictionary_union(ss, new_graph,
								cps_opt_beta_contraction(ss, graph, var_info, kont->u.kproc.body));
	} break;
	}
	return new_graph;
}

sly_value
cps_opt_contraction_phase(Sly_State *ss, sly_value graph, sly_value k, int debug)
{
	sly_value var_info;
	do {
		DELTA = 0;
		var_info = cps_collect_var_info(ss, graph, make_dictionary(ss),
										make_dictionary(ss), NULL, k);
		graph = cps_opt_constant_folding(ss, graph, var_info, k);
		if (debug) {
			cps_display(ss, graph, k);
			printf("================================================\n");
		}
		var_info = cps_collect_var_info(ss, graph, make_dictionary(ss),
										make_dictionary(ss), NULL, k);
		graph = cps_opt_beta_contraction(ss, graph, var_info, k);
		if (debug) {
			cps_display(ss, graph, k);
			printf("================================================\n");
		}
	} while (DELTA);
	return graph;
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

void
cps_display_var_info(Sly_State *ss, sly_value graph, sly_value var_info)
{
	vector *v = GET_PTR(var_info);
	for (size_t i = 0; i < v->cap; ++i) {
		sly_value entry = v->elems[i];
		if (!slot_is_free(entry)) {
			sly_display(car(entry), 1);
			printf(":\n");
			vector *w = GET_PTR(cdr(entry));
			if (w) {
				for (size_t j = 0; j < w->cap; ++j) {
					sly_value entry = w->elems[j];
					if (!slot_is_free(entry)) {
						sly_value name = car(entry);
						CPS_Var_Info *info = GET_PTR(cdr(entry));
						while (info) {
							sly_display(name, 1);
							if (info->binding) {
								printf(" = { used = %d, escapes = %d, binding = ",
									   info->used, info->escapes);
								display_expr(ss, graph, make_dictionary(ss), info->binding);
								printf(" }");
							}
							printf("\n");
							info = info->alt;
						}
						printf("\n");
					}
				}
			}
			printf("\n");
		}
	}
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
				graph = cps_opt_contraction_phase(&ss, graph, entry, 1);
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
