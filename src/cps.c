#include <math.h>
#include <stdio.h>
#include <gc.h>
#include "sly_types.h"
#include "cps.h"

/* CPS intermediate language
 */

/* References:
 * Guile's "CPS Soup"
 * - https://www.gnu.org/software/guile/manual/html_node/CPS-in-Guile.html
 * Blog from a guile dev
 * - https://wingolog.org/
 * - https://wingolog.org/archives/2023/05/20/approaching-cps-soup
 * Book "Compiling with continuations"
 * - Lots of good functional compiler stuff.
 * - Uses Standard ML as example language.
 * - https://www.amazon.com/Compiling-Continuations-Andrew-W-Appel/dp/052103311X
 */

/* Optimization passes:
 * 1. Constant folding/Beta-contraction
 * 2. Beta-expansion (inlining)
 * 3. Eta-reduction
 * 4. Hoisting
 * 5. Common subexpression elimination
 */

/* Phases: (discribed in "Compiling with continuations")
 * - Convert source program into CPS representation
 * - Optimization passes
 * - Closure conversion
 * - Register Spilling
 * - Translate into `abstract-continuation-machine' instructions
 * - Translate into target language. (ie. x86-64)
 */

#if 0
/* NAN Boxing */
#define NAN_BITS 0x7ff0
#define NB_BOOL			(NAN_BITS|0x1) // bool
#define NB_CHAR			(NAN_BITS|0x2) // char
#define NB_INT			(NAN_BITS|0x3) // int
#define NB_PAIR			(NAN_BITS|0x4) // pair
#define NB_SYMBOL		(NAN_BITS|0x5) // symbol
#define NB_BYTE_VECTOR	(NAN_BITS|0x6) // byte-vector
#define NB_STRING		(NAN_BITS|0x7) // string
#define NB_VECTOR		(NAN_BITS|0x8) // vector
#define NB_RECORD		(NAN_BITS|0x9) // record
#define NB_REF		    (NAN_BITS|0xa) // referance
#define NB_CLOSURE	    (NAN_BITS|0xb) // closure
#define NB_TAG_VALUE(type, val_bits) (((type) << 48)|(val_bits))
#define NB_TYPEOF(value) (value >> 48)
#define NB_GET_PTR(value) (value & ((1 << 48) - 1))
#define NB_GET_INTEGRAL(value)  (value & ((1 << 32) - 1))
#endif

#define CAR(val) (syntax_p(val) ? car(syntax_to_datum(val)) : car(val))
#define CDR(val) (syntax_p(val) ? cdr(syntax_to_datum(val)) : cdr(val))
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

#define SYM_SET				cstr_to_symbol("set!")
#define SYM_CALLWCC			cstr_to_symbol("call/cc")
#define SYM_CALLWCC_LONG	cstr_to_symbol("call-with-current-continuation")
#define SYM_VALUES			cstr_to_symbol("values")
#define SYM_CALLWVALUES		cstr_to_symbol("call-with-values")
#define SYM_APPLY			cstr_to_symbol("apply")
#define SYM_LAMBDA			cstr_to_symbol("lambda")
#define SYM_BEGIN			cstr_to_symbol("begin")
#define SYM_IF				cstr_to_symbol("if")
#define SYM_QUOTE			cstr_to_symbol("quote")
#define SYM_SYNTAX_QUOTE	cstr_to_symbol("syntax-quote")
#define SYM_DEFINE			cstr_to_symbol("define")
#define SYM_DEFINE_SYNTAX	cstr_to_symbol("define-syntax")

static sly_value prim_void(Sly_State *ss, sly_value arg_list);
static sly_value prim_add(Sly_State *ss, sly_value arg_list);
static sly_value prim_sub(Sly_State *ss, sly_value arg_list);
static sly_value prim_mul(Sly_State *ss, sly_value arg_list);
static sly_value prim_div(Sly_State *ss, sly_value arg_list);
static sly_value prim_idiv(Sly_State *ss, sly_value arg_list);
static sly_value prim_mod(Sly_State *ss, sly_value arg_list);
static sly_value prim_num_eq(Sly_State *ss, sly_value arg_list);
static sly_value prim_less(Sly_State *ss, sly_value arg_list);
static sly_value prim_gr(Sly_State *ss, sly_value arg_list);
static sly_value prim_leq(Sly_State *ss, sly_value arg_list);
static sly_value prim_geq(Sly_State *ss, sly_value arg_list);
static sly_value prim_cons(Sly_State *ss, sly_value arg_list);
static sly_value prim_car(Sly_State *ss, sly_value arg_list);
static sly_value prim_cdr(Sly_State *ss, sly_value arg_list);
static sly_value prim_list(Sly_State *ss, sly_value arg_list);
static sly_value prim_vector(Sly_State *ss, sly_value arg_list);

static struct primop primops[] = {
	[tt_prim_void]			= {"void", .fn = prim_void},
	[tt_prim_add]			= {"+", .fn = prim_add},    // (+ . args)
	[tt_prim_sub]			= {"-", .fn = prim_sub},    // (- . args)
	[tt_prim_mul]			= {"*", .fn = prim_mul},    // (* . args)
	[tt_prim_div]			= {"/", .fn = prim_div},    // (/ . args)
	[tt_prim_idiv]			= {"div", .fn = prim_idiv}, // (div . args)
	[tt_prim_mod]			= {"%", .fn = prim_mod},    // (% x y)
	[tt_prim_bw_and]		= {"bitwise-and"},          // (bitwise-and x y)
	[tt_prim_bw_ior]		= {"bitwise-ior"},          // (bitwise-ior x y)
	[tt_prim_bw_xor]		= {"bitwise-xor"},          // (bitwise-xor x y)
	[tt_prim_bw_eqv]		= {"bitwise-eqv"},          // (bitwise-eqv x y)
	[tt_prim_bw_nor]		= {"bitwise-nor"},          // (bitwise-nor x y)
	[tt_prim_bw_nand]		= {"bitwise-nand"},         // (bitwise-nand x y)
	[tt_prim_bw_not]		= {"bitwise-not"},          // (bitwise-not x y)
	[tt_prim_bw_shift]		= {"arithmetic-shift"},     // (bitwise-shift x y)
	[tt_prim_eq]			= {"eq?"},
	[tt_prim_eqv]			= {"eqv?"},
	[tt_prim_equal]			= {"equal?"},
	[tt_prim_num_eq]		= {"=", .fn = prim_num_eq},
	[tt_prim_less]			= {"<", .fn = prim_less},
	[tt_prim_gr]			= {">", .fn = prim_gr},
	[tt_prim_leq]			= {"<=", .fn = prim_leq},
	[tt_prim_geq]			= {">=", .fn = prim_geq},
	[tt_prim_cons]			= {"cons", .fn = prim_cons},
	[tt_prim_car]			= {"car", .fn = prim_car},
	[tt_prim_cdr]			= {"cdr", .fn = prim_cdr},
	[tt_prim_list]			= {"list", .fn = prim_list},
	[tt_prim_vector]		= {"vector", .fn = prim_vector},
	[tt_prim_vector_ref]	= {"vector-ref"},
	[tt_prim_vector_set]	= {"vector-set"},
};

UNUSED_ATTR static int NGPR = 32;
UNUSED_ATTR static int NFPR = 32;

#define CLICK() click(1)
#define CLICK_RESET() click(0)

static size_t
click(int x)
{
	static size_t delta = 0;
	if (x) {
		delta++;
		return delta;
	} else {
		size_t tmp = delta;
		delta = 0;
		return tmp;
	}
}

static sly_value
prim_void(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	sly_assert(list_len(arg_list) == 0, "Error void primitive expects zero arguments");
	return SLY_VOID;
}

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
	sly_value res = sly_div(ss, fst, total);
	if (float_p(res) && isinf(get_float(res))) {

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
prim_num_eq(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	if (list_len(arg_list) < 2) {
		return SLY_TRUE;
	}
	sly_value x = car(arg_list);
	sly_value y;
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		y = car(arg_list);
		if (!sly_num_eq(x, y)) {
			return SLY_FALSE;
		}
		x = y;
		arg_list = cdr(arg_list);
	}
	return SLY_TRUE;
}

static sly_value
prim_less(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	if (list_len(arg_list) < 2) {
		return SLY_TRUE;
	}
	sly_value x = car(arg_list);
	sly_value y;
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		y = car(arg_list);
		if (!sly_num_lt(x, y)) {
			return SLY_FALSE;
		}
		x = y;
		arg_list = cdr(arg_list);
	}
	return SLY_TRUE;
}

static sly_value
prim_gr(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	if (list_len(arg_list) < 2) {
		return SLY_TRUE;
	}
	sly_value x = car(arg_list);
	sly_value y;
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		y = car(arg_list);
		if (!sly_num_gt(x, y)) {
			return SLY_FALSE;
		}
		x = y;
		arg_list = cdr(arg_list);
	}
	return SLY_TRUE;
}

static sly_value
prim_leq(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	if (list_len(arg_list) < 2) {
		return SLY_TRUE;
	}
	sly_value x = car(arg_list);
	sly_value y;
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		y = car(arg_list);
		if (!(sly_num_lt(x, y) || sly_num_eq(x, y))) {
			return SLY_FALSE;
		}
		x = y;
		arg_list = cdr(arg_list);
	}
	return SLY_TRUE;
}

static sly_value
prim_geq(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	if (list_len(arg_list) < 2) {
		return SLY_TRUE;
	}
	sly_value x = car(arg_list);
	sly_value y;
	arg_list = cdr(arg_list);
	while (!null_p(arg_list)) {
		y = car(arg_list);
		if (!(sly_num_gt(x, y) || sly_num_eq(x, y))) {
			return SLY_FALSE;
		}
		x = y;
		arg_list = cdr(arg_list);
	}
	return SLY_TRUE;
}

static sly_value
prim_cons(Sly_State *ss, sly_value arg_list)
{
	sly_assert(list_len(arg_list) == 2, "Error cons requires 2 arguments");
	return cons(ss, car(arg_list), car(cdr(arg_list)));
}

static sly_value
prim_car(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	sly_assert(list_len(arg_list) == 1, "Error car requires 2 arguments");
	return car(car(arg_list));
}

static sly_value
prim_cdr(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	sly_assert(list_len(arg_list) == 1, "Error cdr requires 2 arguments");
	return cdr(car(arg_list));
}

static sly_value
prim_list(UNUSED_ATTR Sly_State *ss, sly_value arg_list)
{
	return arg_list;
}

static sly_value
prim_vector(Sly_State *ss, sly_value arg_list)
{
	return list_to_vector(ss, arg_list);
}

void
cps_init_primops(Sly_State *ss)
{
	for (size_t i = 0; i < ARR_LEN(primops); ++i) {
		char *cstr = primops[i].cstr;
		primops[i].name = make_symbol(ss, cstr, strlen(cstr));
	}
}

int
primop_p(sly_value name)
{
	if (identifier_p(name)) {
		name = strip_syntax(name);
	} else if (!symbol_p(name)) {
		return -1;
	}
	for (size_t i = 0; i < ARR_LEN(primops); ++i) {
		if (symbol_eq(name, primops[i].name)) {
			return i;
		}
	}
	return -1;
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

CPS_Expr *
cps_make_fix(void)
{
	CPS_Expr *fix = cps_new_expr();
	fix->type = tt_cps_fix;
	fix->u.fix.names = SLY_NULL;
	fix->u.fix.procs = SLY_NULL;
	return fix;
}

CPS_Var_Info *
cps_new_var_info(CPS_Expr *binding, int isarg, int isalias, int which)
{
	CPS_Var_Info *vi = GC_MALLOC(sizeof(*vi));
	vi->used = 0;
	vi->escapes = 0;
	vi->updates = 0;
	vi->binding = binding;
	vi->isarg = isarg;
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
		c->name = gensym_from_cstr(ss, "#ktail");
	}
	return c;
}

sly_value
cps_gensym_temporary_name(Sly_State *ss)
{
	return gensym_from_cstr(ss, "_t");
}

static sly_value
cps_gensym_label_name(Sly_State *ss)
{
	return gensym_from_cstr(ss, "_k");
}

static CPS_Kont *
cps_copy_kont(CPS_Kont *k)
{
	CPS_Kont *j = GC_MALLOC(sizeof(*j));
	*j = *k;
	return j;
}

static CPS_Kont *
cps_deep_copy_kont(CPS_Kont *k)
{
	CPS_Kont *j = cps_copy_kont(k);
	if (k->type == tt_cps_kargs) {
		j->u.kargs.term = cps_new_term();
		if (k->u.kargs.term->type == tt_cps_continue) {
			*j->u.kargs.term = *k->u.kargs.term;
			j->u.kargs.term->u.cont.expr = cps_new_expr();
			*j->u.kargs.term->u.cont.expr = *k->u.kargs.term->u.cont.expr;
		} else if (k->u.kargs.term->type == tt_cps_branch) {
			*j->u.kargs.term = *k->u.kargs.term;
		} else {
			sly_assert(0, "Error unreachable");
		}
	}
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

static sly_value
_cps_translate(Sly_State *ss, CPS_Expr *fix, sly_value cc,
			   sly_value graph, sly_value form)
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
			if (symbol_eq(strip_syntax(fst), SYM_DEFINE)
				|| symbol_eq(strip_syntax(fst), SYM_DEFINE_SYNTAX)) {
				sly_value name = strip_syntax(CAR(rest));
				sly_value value = CAR(CDR(rest));
				sly_value kname = cps_gensym_label_name(ss);
				sly_value kk = _cps_translate(ss, fix, kname, graph, value);
				{
					CPS_Expr *expr = cps_new_expr();
					expr->type = tt_cps_box;
					expr->u.box.val = SLY_VOID;
					fix->u.fix.names = cons(ss, name, fix->u.fix.names);
					fix->u.fix.procs = cons(ss, (sly_value)expr, fix->u.fix.procs);
				}
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_set;
				t->u.cont.expr->u.set.var = name;
				sly_value args = make_list(ss, 1, cps_gensym_temporary_name(ss));
				t->u.cont.expr->u.set.val = car(args);
				k = cps_make_kargs(ss, kname, t, args);
				t->u.cont.k = cc;
				cps_graph_set(ss, graph, k->name, k);
				return kk;
			} else if (symbol_eq(strip_syntax(fst), SYM_SET)) {
				sly_value name = strip_syntax(CAR(rest));
				sly_value kname = cps_gensym_label_name(ss);
				sly_value kk = _cps_translate(ss, fix, kname, graph,
											  CAR(CDR(rest)));
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_set;
				t->u.cont.expr->u.set.var = name;
				sly_value args = make_list(ss, 1, cps_gensym_temporary_name(ss));
				t->u.cont.expr->u.set.val = car(args);
				k = cps_make_kargs(ss, kname, t, args);
				t->u.cont.k = cc;
				cps_graph_set(ss, graph, k->name, k);
				return kk;
			} else if (symbol_eq(strip_syntax(fst), SYM_VALUES)) {
				sly_value vlist = SLY_NULL;
				sly_value e;
				t = cps_new_term();
				t->u.cont.k = cc;
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				sly_value tmp = cps_gensym_temporary_name(ss);
				k = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
								   make_list(ss, 1, tmp));
				cc = k->name;
				cps_graph_set(ss, graph, cc, k);
				sly_value name;
				while (!null_p(rest)) {
					e = CAR(rest);
					if (identifier_p(e) || symbol_p(e)) {
						name = strip_syntax(e);
					} else { // constant or expression
						cc = _cps_translate(ss, fix, cc, graph, e);
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
			} else if (symbol_eq(strip_syntax(fst), SYM_LAMBDA)) {
				sly_value arg_formals = CAR(rest);
				CPS_Kont *tail = cps_make_ktail(ss, 1);
				sly_value kp = tail->name;
				cps_graph_set(ss, graph, kp, tail);
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_proc;
				t->u.cont.k = cc;
				k = cps_make_ktail(ss, 0);
				k->type = tt_cps_kproc;
				k->name = cps_gensym_label_name(ss);
				k->u.kproc.tail = kp;
				form = cons(ss, SYM_BEGIN, CDR(rest));
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
					sly_assert(kont->type == tt_cps_kargs,
							   "Error expected kargs");
					kont->u.kargs.vars = args;
				}
				k->u.kproc.arity.req = req;
				t->u.cont.expr->u.proc.k = k->name;
				cps_graph_set(ss, graph, k->name, k);
				{
					sly_value name = cps_gensym_label_name(ss);
					cc = name;
					sly_value tv = cps_gensym_temporary_name(ss);
					k = cps_make_kargs(ss, name, t, make_list(ss, 1, tv));
					cps_graph_set(ss, graph, cc, k);
				}
				return cc;
			} else if (symbol_eq(strip_syntax(fst), SYM_BEGIN)) {
				if (!null_p(CDR(rest))) {
					cc = _cps_translate(ss, fix, cc, graph,
										cons(ss, fst, CDR(rest)));
				}
				return _cps_translate(ss, fix, cc, graph, CAR(rest));
			} else if (symbol_eq(strip_syntax(fst), SYM_IF)) {
				sly_value cform = CAR(rest);
				sly_value tform = CAR(CDR(rest));
				sly_value fform = CAR(CDR(CDR(rest)));
				sly_value kt, kf;
				kt = _cps_translate(ss, fix, cc, graph, tform);
				kf = _cps_translate(ss, fix, cc, graph, fform);
				{ /* Branch term does not pass values to it's continuations,
				   * so their bindings start dead. Let's just remove them here.
				   */
					CPS_Kont *tmp = cps_graph_ref(graph, kt);
					sly_assert(tmp->type == tt_cps_kargs,
							   "Error expected kargs");
					tmp->u.kargs.vars = SLY_NULL;
					tmp = cps_graph_ref(graph, kf);
					sly_assert(tmp->type == tt_cps_kargs,
							   "Error expected kargs");
					tmp->u.kargs.vars = SLY_NULL;
				}
				t = cps_new_term();
				sly_value tl = cps_gensym_label_name(ss);
				sly_value tv = cps_gensym_temporary_name(ss);
				k = cps_make_kargs(ss, tl, t, make_list(ss, 1, tv));
				t->type = tt_cps_branch;
				t->u.branch.arg = car(k->u.kargs.vars);
				t->u.branch.kt = kt;
				t->u.branch.kf = kf;
				cc = k->name;
				cps_graph_set(ss, graph, cc, k);
				return _cps_translate(ss, fix, cc, graph, cform);
			} else if (symbol_eq(strip_syntax(fst), SYM_QUOTE)) {
				form = CAR(rest);
				CPS_CONSTANT();
				return cc;
			} else if (symbol_eq(strip_syntax(fst), SYM_SYNTAX_QUOTE)) {
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
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
						   make_list(ss, 1, tmp));
		cc = k->name;
		cps_graph_set(ss, graph, cc, k);
		while (!null_p(form)) {
			e = CAR(form);
			sly_value name;
			if (identifier_p(e) || symbol_p(e)) {
				name = strip_syntax(e);
			} else { // constant or expression
				cc = _cps_translate(ss, fix, cc, graph, e);
				k = cps_graph_ref(graph, cc);
				name = tmp;
				tmp = car(k->u.kargs.vars);
			}
			vlist = list_append(ss, vlist, cons(ss, name, SLY_NULL));
			form = CDR(form);
		}
		sly_value name = car(vlist);
		vlist = cdr(vlist);
		if (primop_p(name) != -1) {
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
		if (primop_p(s) != -1) {
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

sly_value
cps_translate(Sly_State *ss, sly_value cc, sly_value graph, sly_value form)
{
	CPS_Expr *fix = cps_make_fix();
	sly_value entry = _cps_translate(ss, fix, cc, graph, form);
	if (!null_p(fix->u.fix.names)) {
		CPS_Term *t = cps_new_term();
		t->type = tt_cps_continue;
		t->u.cont.expr = fix;
		t->u.cont.k = entry;
		CPS_Kont *kont =
			cps_make_kargs(ss, cps_gensym_label_name(ss), t, SLY_NULL);
		cps_graph_set(ss, graph, kont->name, kont);
		CPS_Kont *e = cps_graph_ref(graph, entry);
		e->u.kargs.vars = fix->u.fix.names;
		entry = kont->name;
	}
	return entry;
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

static CPS_Var_Info *
cps_var_def_info(Sly_State *ss, sly_value tbl, sly_value name, CPS_Expr *expr,
				 int isarg, int isalias, int which)
{
	CPS_Var_Info *vi = GET_PTR(dictionary_ref(tbl, name, SLY_VOID));
	if (vi == NULL) {
		vi = cps_new_var_info(expr, isarg, isalias, which);
		dictionary_set(ss, tbl, name, (sly_value)vi);
	} else {
		vi->isarg = isarg;
		vi->isalias = isalias;
		vi->which = which;
		vi->binding = expr;
	}
	return vi;
}

static void
cps_var_info_set_binding(sly_value tbl, sly_value name, CPS_Expr *binding)
{
	CPS_Var_Info *vi = GET_PTR(dictionary_ref(tbl, name, SLY_VOID));
	if (vi == NULL) {
		return;
	}
	vi->binding = binding;
}

static void
cps_var_info_inc_used(Sly_State *ss, sly_value tbl, sly_value name)
{
	CPS_Var_Info *vi = GET_PTR(dictionary_ref(tbl, name, SLY_VOID));
	if (vi == NULL) {
		vi = cps_var_def_info(ss, tbl, name, NULL, 0, 0, 0);
	}
	vi->used++;
}

static void
cps_var_info_inc_updates(Sly_State *ss, sly_value tbl, sly_value name)
{
	CPS_Var_Info *vi = GET_PTR(dictionary_ref(tbl, name, SLY_VOID));
	if (vi == NULL) {
		vi = cps_var_def_info(ss, tbl, name, NULL, 0, 0, 0);
	}
	vi->updates++;
}

static void
cps_var_info_inc_escapes(Sly_State *ss, sly_value tbl, sly_value name)
{
	CPS_Var_Info *vi = GET_PTR(dictionary_ref(tbl, name, SLY_VOID));
	if (vi == NULL) {
		vi = cps_var_def_info(ss, tbl, name, NULL, 0, 0, 0);
	}
	vi->escapes++;
}

static void
cps_var_info_visit_expr(Sly_State *ss, sly_value global_tbl, sly_value tbl,
						CPS_Expr *expr)
{
	sly_value args;
	switch (expr->type) {
	case tt_cps_primcall: {
		args = expr->u.primcall.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			cps_var_info_inc_used(ss, tbl, var);
			cps_var_info_inc_used(ss, global_tbl, var);
			args = cdr(args);
		}
	} break;
	case tt_cps_call: {
		sly_value proc = expr->u.call.proc;
		cps_var_info_inc_used(ss, tbl, proc);
		cps_var_info_inc_used(ss, global_tbl, proc);
		args = expr->u.call.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			if (var == SLY_FALSE) {
				continue;
			}
			cps_var_info_inc_used(ss, tbl, var);
			cps_var_info_inc_escapes(ss, tbl, var);
			cps_var_info_inc_used(ss, global_tbl, var);
			cps_var_info_inc_escapes(ss, global_tbl, var);
			args = cdr(args);
		}
	} break;
	case tt_cps_kcall: {
		sly_assert(0, "unimplemented");
		args = expr->u.kcall.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			cps_var_info_inc_used(ss, tbl, var);
			cps_var_info_inc_used(ss, global_tbl, var);
			args = cdr(args);
		}
	} break;
	case tt_cps_values: {
		args = expr->u.values.args;
		while (!null_p(args)) {
			sly_value var = car(args);
			cps_var_info_inc_used(ss, tbl, var);
			cps_var_info_inc_used(ss, global_tbl, var);
			args = cdr(args);
		}
	} break;
	case tt_cps_set: {
		/* NOTE: If a variable is set, it should be marked "updated" but
		 * not "used".
		 * This way the variable can potentially be constant folded
		 * even after its value changes.
		 */
		/* cps_var_info_inc_used(ss, global_tbl, expr->u.set.var); */
		/* cps_var_info_inc_used(ss, tbl, expr->u.set.var); */
		cps_var_info_inc_updates(ss, tbl, expr->u.set.var);
		cps_var_info_inc_updates(ss, global_tbl, expr->u.set.var);
		cps_var_info_inc_used(ss, tbl, expr->u.set.val);
		cps_var_info_inc_used(ss, global_tbl, expr->u.set.val);
	} break;
	case tt_cps_fix: {
		sly_value n, names = expr->u.fix.names;
		sly_value procs = expr->u.fix.procs;
		CPS_Expr *e;
		while (!null_p(names)) {
			n = car(names);
			e = GET_PTR(car(procs));
			cps_var_def_info(ss, tbl, n, e, 0, 0, 0);
			cps_var_def_info(ss, global_tbl, n, e, 0, 0, 0);
			names = cdr(names);
			procs = cdr(procs);
		}
	} break;
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

sly_value
cps_collect_var_info(Sly_State *ss, sly_value graph, sly_value global_tbl,
					 sly_value state, sly_value prev_tbl, CPS_Expr *expr,
					 sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_kargs: {
		sly_value vlist = kont->u.kargs.vars;
		sly_value var_tbl = copy_dictionary(ss, prev_tbl);
		int isalias = expr && (expr->type == tt_cps_values);
		int w = 0;
		CPS_Term *term = kont->u.kargs.term;
		if (expr && expr->type != tt_cps_fix) {
			while (!null_p(vlist)) {
				cps_var_def_info(ss, var_tbl, car(vlist), expr, 0, isalias, w);
				cps_var_def_info(ss, global_tbl, car(vlist), expr, 0, isalias, w);
				w++;
				vlist = cdr(vlist);
			}
		}
		dictionary_set(ss, state, kont->name, var_tbl);
		switch (term->type) {
		case tt_cps_continue: {
			CPS_Expr *nexpr = term->u.cont.expr;
			if (nexpr->type == tt_cps_proc) {
				state = cps_collect_var_info(ss, graph, global_tbl, state,
											 var_tbl, nexpr, nexpr->u.proc.k);
			} else if (nexpr->type == tt_cps_fix) {
				sly_value procs = nexpr->u.fix.procs;
				CPS_Expr *p;
				cps_var_info_visit_expr(ss, global_tbl, var_tbl, nexpr);
				while (!null_p(procs)) {
					p = GET_PTR(car(procs));
					if (p->type == tt_cps_proc) {
						state = cps_collect_var_info(ss, graph, global_tbl,
													 state, var_tbl, p, p->u.proc.k);
					}
					procs = cdr(procs);
				}
			} else if (nexpr->type == tt_cps_set) {
				cps_var_info_set_binding(var_tbl, nexpr->u.set.var, expr);
				cps_var_info_visit_expr(ss, global_tbl, var_tbl, nexpr);
			} else {
				cps_var_info_visit_expr(ss, global_tbl, var_tbl, nexpr);
			}
			state = cps_collect_var_info(ss, graph, global_tbl,
										 state, var_tbl, nexpr, term->u.cont.k);
		} break;
		case tt_cps_branch: {
			cps_var_info_inc_used(ss, var_tbl, term->u.branch.arg);
			cps_var_info_inc_used(ss, global_tbl, term->u.branch.arg);
			sly_value b1, b2;
			b1 = copy_dictionary(ss, state);
			b2 = copy_dictionary(ss, state);
			b1 = cps_collect_var_info(ss, graph, global_tbl, b1, var_tbl, NULL,
									  term->u.branch.kt);
			b2 = cps_collect_var_info(ss, graph, global_tbl, b2, var_tbl, NULL,
									  term->u.branch.kf);
			state = cps_var_info_propagate(ss, state,
										   cps_var_info_propagate(ss, b1, b2));
		} break;
		}
	} break;
	case tt_cps_kreceive: {
		state = cps_collect_var_info(ss, graph, global_tbl, state, prev_tbl,
									 expr, kont->u.kreceive.k);
	} break;
	case tt_cps_kproc: {
		sly_value req = kont->u.kproc.arity.req;
		sly_value rest = kont->u.kproc.arity.rest;
		sly_value var_tbl = copy_dictionary(ss, prev_tbl);
		while (pair_p(req)) {
			cps_var_def_info(ss, var_tbl, car(req), NULL, 1, 0, 0);
			cps_var_def_info(ss, global_tbl, car(req), NULL, 1, 0, 0);
			req = cdr(req);
		}
		if (rest != SLY_FALSE) {
			cps_var_def_info(ss, var_tbl, rest, NULL, 1, 0, 0);
			cps_var_def_info(ss, global_tbl, rest, NULL, 1, 0, 0);
		}
		state = cps_collect_var_info(ss, graph, global_tbl, state, var_tbl,
									 NULL, kont->u.kproc.body);
	} break;
	case tt_cps_ktail: break;
	default: {
		sly_assert(0, "Error Invalid continuation");
	} break;
	}
	return state;
}

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
	int updates = 0;
	while (vi) {
		used += vi->used;
		escapes += vi->escapes;
		updates += vi->updates;
		vi = vi->alt;
	}
	return (used + escapes + updates) == 0;
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
cps_opt_constant_folding_visit_expr(Sly_State *ss, sly_value var_info,
									CPS_Expr *expr)
{
	sly_value args = expr->u.primcall.args;
	sly_value val_list = SLY_NULL;
	if (expr->type == tt_cps_primcall) {
		enum prim op = primop_p(expr->u.primcall.prim);
		switch (op) {
		case tt_prim_void: break;
		case tt_prim_add:
		case tt_prim_sub:
		case tt_prim_mul:
		case tt_prim_div:
		case tt_prim_idiv:
		case tt_prim_num_eq:
		case tt_prim_less:
		case tt_prim_gr:
		case tt_prim_leq:
		case tt_prim_geq: {
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
		case tt_prim_eq: {
			sly_assert(list_len(args) == 2, "Error arity mismatch");
			sly_value v1 = cps_get_const(var_info, car(args));
			if (v1 == SLY_VOID) {
				return expr;
			}
			sly_value v2 = cps_get_const(var_info, car(cdr(args)));
			if (v2 == SLY_VOID) {
				return expr;
			}
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = ctobool(sly_eq(v1, v2));
			return expr;
		} break;
		case tt_prim_eqv: {
			sly_assert(list_len(args) == 2, "Error arity mismatch");
			sly_value v1 = cps_get_const(var_info, car(args));
			if (v1 == SLY_VOID) {
				return expr;
			}
			sly_value v2 = cps_get_const(var_info, car(cdr(args)));
			if (v2 == SLY_VOID) {
				return expr;
			}
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = ctobool(sly_eqv(v1, v2));
			return expr;
		} break;
		case tt_prim_equal: {
			sly_assert(list_len(args) == 2, "Error arity mismatch");
			sly_value v1 = cps_get_const(var_info, car(args));
			if (v1 == SLY_VOID) {
				return expr;
			}
			sly_value v2 = cps_get_const(var_info, car(cdr(args)));
			if (v2 == SLY_VOID) {
				return expr;
			}
			expr = cps_new_expr();
			expr->type = tt_cps_const;
			expr->u.constant.value = ctobool(sly_equal(v1, v2));
			return expr;
		} break;
		case tt_prim_cons: {
			sly_assert(list_len(args) == 2, "Error arity mismatch");
			sly_value v1 = cps_get_const(var_info, car(args));
			if (v1 == SLY_VOID) {
				return expr;
			}
			sly_value v2 = cps_get_const(var_info, car(cdr(args)));
			if (v2 == SLY_VOID) {
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
		case tt_prim_vector_ref: break;
		case tt_prim_vector_set: break;
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
		case tt_prim_vector: {
			while (!null_p(args)) {
				sly_value val = cps_get_const(var_info, car(args));
				if (void_p(val)) {
					return expr;
				}
				val_list = list_append(ss, val_list,
									   cons(ss, val, SLY_NULL));
				args = cdr(args);
			}
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
	// TODO: Some primops may have side effects,
	// for example divide by zero exception.
	return expr->type == tt_cps_const
		|| expr->type == tt_cps_proc
		|| expr->type == tt_cps_prim
		|| expr->type == tt_cps_values;
}

static sly_value
list_remove_idx(Sly_State *ss, sly_value lst, size_t idx)
{
	sly_assert(!null_p(lst), "Error list out of bounds");
	if (idx == 0) {
		return cdr(lst);
	}
	return cons(ss, car(lst), list_remove_idx(ss, cdr(lst), idx-1));
}

static sly_value
cps_opt_constant_folding(Sly_State *ss, sly_value graph,
						 sly_value global_var_info,
						 sly_value var_info, sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	sly_value new_graph = make_dictionary(ss);
	cps_graph_set(ss, new_graph, k, kont);
	switch (kont->type) {
	case tt_cps_kargs: {
		sly_value info = dictionary_ref(var_info, k, SLY_VOID);
		sly_assert(!void_p(info), "Error No var info");
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			CPS_Expr *expr =
				cps_opt_constant_folding_visit_expr(ss, info, term->u.cont.expr);
			CPS_Kont *new_kont = cps_copy_kont(kont);
			dictionary_set(ss, new_graph, k, (sly_value)new_kont);
			sly_value nk = term->u.cont.k;
			if (expr != term->u.cont.expr) {
				CLICK();
				new_kont->u.kargs.term = cps_new_term();
				term = new_kont->u.kargs.term;
				term->u.cont.expr = expr;
				CPS_Kont *receive = cps_graph_ref(graph, nk);
				if (receive->type == tt_cps_kreceive) {
					 // expression folded, don't need receive
					term->u.cont.k = receive->u.kreceive.k;
				} else {
					term->u.cont.k = nk;
				}
				sly_value tmp =
					cps_opt_constant_folding(ss, graph, global_var_info,
											 var_info, term->u.cont.k);
				return dictionary_union(ss, new_graph, tmp);
			}
			if (expr->type == tt_cps_proc) {
				sly_value tmp =
					cps_opt_constant_folding(ss, graph, global_var_info,
											 var_info, expr->u.proc.k);
				new_graph = dictionary_union(ss, new_graph, tmp);
			}
			CPS_Kont *next = cps_graph_ref(graph, nk);
			if (expr->type == tt_cps_fix) {
				sly_value names = expr->u.fix.names;
				sly_value procs = expr->u.fix.procs;
				sly_value name;
				CPS_Expr *p;
				int idx = 0;
				if (null_p(names)) {
					CLICK();
					printf("next->type = %d\n", next->type);
					if (next->type == tt_cps_kargs) {
						*term = *next->u.kargs.term;
					} else if (next->type == tt_cps_ktail) {
						cps_graph_set(ss, new_graph, next->name, next);
						term->u.cont.expr = cps_new_expr();
						term->u.cont.expr->type = tt_cps_const;
						term->u.cont.expr->u.constant.value = SLY_VOID;
						return new_graph;
					}
				} else {
					while (!null_p(names)) {
						name = car(names);
						if (var_is_dead(global_var_info, name)) {
							printf("HERE\n");
							sly_displayln(name);
							CLICK();
							expr->u.fix.names =
								list_remove_idx(ss, expr->u.fix.names, idx);
							expr->u.fix.procs =
								list_remove_idx(ss, expr->u.fix.procs, idx);
							next->u.kargs.vars =
								list_remove_idx(ss, next->u.kargs.vars, idx);
							idx--;
						} else {
							p = GET_PTR(car(procs));
							if (p->type == tt_cps_proc) {
								sly_value tmp =
									cps_opt_constant_folding(ss, graph,
															 global_var_info,
															 var_info, p->u.proc.k);
								new_graph = dictionary_union(ss, new_graph, tmp);
							}
						}
						idx++;
						names = cdr(names);
						procs = cdr(procs);
					}
				}
			}
			nk = new_kont->u.kargs.term->u.cont.k;
			next = cps_graph_ref(graph, nk);
			if (next->type == tt_cps_kreceive
				&& list_len(next->u.kreceive.arity.req) == 1
				&& next->u.kreceive.arity.rest == SLY_FALSE
				&& expr->type == tt_cps_call) {
				CPS_Kont *kr = cps_graph_ref(graph, next->u.kreceive.k);
				if (kr->type == tt_cps_kargs
					&& kr->u.kargs.term->type == tt_cps_continue) {
					CPS_Kont *kt = cps_graph_ref(graph, kr->u.kargs.term->u.cont.k);
					CPS_Expr *e = kr->u.kargs.term->u.cont.expr;
					if (kt->type == tt_cps_ktail
						&& e->type == tt_cps_values
						&& sly_equal(kr->u.kargs.vars, e->u.values.args)) {
						CLICK();
						term->u.cont.k = kt->name;
						sly_value tmp =
							cps_opt_constant_folding(ss, graph, global_var_info,
													 var_info, term->u.cont.k);
						return dictionary_union(ss, new_graph, tmp);
					}
				}
			} else if (expr->type == tt_cps_set
					   && next->type == tt_cps_kargs
					   && list_len(next->u.kargs.vars) == 1
					   && var_is_dead(global_var_info, car(next->u.kargs.vars))) {
				CLICK();
				next->u.kargs.vars = SLY_NULL;
			} else if (expr_has_no_possible_side_effect(expr)
					   && next->type == tt_cps_kargs
					   && list_len(next->u.kargs.vars) == 1) {
				if (var_is_dead(global_var_info, car(next->u.kargs.vars))) {
					new_kont->u.kargs.term = next->u.kargs.term;
					term = new_kont->u.kargs.term;
					expr = term->u.cont.expr;
					CLICK();
					if (term->type == tt_cps_branch) {
						kont = new_kont;
						goto handle_branch;
					} else if (expr->type == tt_cps_proc) {
						sly_value k = expr->u.proc.k;
						sly_value tmp =
							cps_opt_constant_folding(ss, graph, global_var_info,
													 var_info, k);
						new_graph = dictionary_union(ss, new_graph, tmp);
					} else if (expr->type == tt_cps_fix) {
						sly_value names = expr->u.fix.names;
						sly_value procs = expr->u.fix.procs;
						sly_value name;
						CPS_Expr *p;
						int idx = 0;
						if (null_p(names)) {
							CLICK();
							*term = *next->u.kargs.term;
						} else {
							while (!null_p(names)) {
								name = car(names);
								if (var_is_dead(global_var_info, name)) {
									CLICK();
									expr->u.fix.names =
										list_remove_idx(ss, expr->u.fix.names, idx);
									expr->u.fix.procs =
										list_remove_idx(ss, expr->u.fix.procs, idx);
									next->u.kargs.vars =
										list_remove_idx(ss, next->u.kargs.vars, idx);
									idx--;
								} else {
									p = GET_PTR(car(procs));
									sly_value tmp =
										cps_opt_constant_folding(ss, graph,
																 global_var_info,
																 var_info, p->u.proc.k);
									new_graph = dictionary_union(ss, new_graph, tmp);
								}
								idx++;
								names = cdr(names);
								procs = cdr(procs);
							}
						}
					}
					sly_value tmp = cps_opt_constant_folding(ss, graph, global_var_info,
															 var_info, term->u.cont.k);
					return dictionary_union(ss, new_graph, tmp);
				}
			} else if (expr->type == tt_cps_values
					   && next->type == tt_cps_kargs
					   && list_len(next->u.kargs.vars) == 1
					   && var_is_used_once(global_var_info, car(expr->u.values.args))) {
				sly_value tmp =
					dictionary_ref(info, car(expr->u.values.args), SLY_VOID);
				CPS_Var_Info *vi = GET_PTR(tmp);
				if (vi->binding
					&& expr_has_no_possible_side_effect(vi->binding)) {
					CLICK();
					*expr = *vi->binding;
					tmp = cps_opt_constant_folding(ss, graph, global_var_info,
												   var_info, new_kont->name);
					return dictionary_union(ss, new_graph, tmp);
				}
			} else if (expr->type == tt_cps_values
					   && next->type == tt_cps_kargs
					   && list_len(expr->u.values.args) == 1
					   && list_len(new_kont->u.kargs.vars) == 1
					   && list_len(next->u.kargs.vars) == 1
					   && sly_equal(car(new_kont->u.kargs.vars),
									car(expr->u.values.args))
					   && var_is_used_once(info, car(expr->u.values.args))) {
				CLICK();
				new_kont->u.kargs.vars = next->u.kargs.vars;
				new_kont->u.kargs.term = next->u.kargs.term;
				term = new_kont->u.kargs.term;
				if (term->type == tt_cps_branch) {
					kont = new_kont;
					goto handle_branch;
				} else if (expr->type == tt_cps_proc) {
					sly_value k = expr->u.proc.k;
					sly_value tmp =
						cps_opt_constant_folding(ss, graph, global_var_info,
												 var_info, k);
					new_graph = dictionary_union(ss, new_graph, tmp);
				} else if (expr->type == tt_cps_fix) {
					sly_value names = expr->u.fix.names;
					sly_value procs = expr->u.fix.procs;
					sly_value name;
					CPS_Expr *p;
					int idx = 0;
					if (null_p(names)) {
						CLICK();
						*term = *next->u.kargs.term;
					} else {
						while (!null_p(names)) {
							name = car(names);
							if (var_is_dead(global_var_info, name)) {
								CLICK();
								expr->u.fix.names =
									list_remove_idx(ss, expr->u.fix.names, idx);
								expr->u.fix.procs =
									list_remove_idx(ss, expr->u.fix.procs, idx);
								next->u.kargs.vars =
									list_remove_idx(ss, next->u.kargs.vars, idx);
								idx--;
							} else {
								p = GET_PTR(car(procs));
								sly_value tmp =
									cps_opt_constant_folding(ss, graph,
															 global_var_info,
															 var_info, p->u.proc.k);
								new_graph = dictionary_union(ss, new_graph, tmp);
							}
							idx++;
							names = cdr(names);
							procs = cdr(procs);
						}
					}
				}
				sly_value tmp = cps_opt_constant_folding(ss, graph, global_var_info,
														 var_info, term->u.cont.k);
				return dictionary_union(ss, new_graph, tmp);
			}
			sly_value tmp = cps_opt_constant_folding(ss, graph, global_var_info,
													 var_info, term->u.cont.k);
			return dictionary_union(ss, new_graph, tmp);
		} else if (term->type == tt_cps_branch) handle_branch: {
			sly_value arg = cps_get_const(info, term->u.branch.arg);
			if (void_p(arg)) {
				sly_value tmp =
					cps_opt_constant_folding(ss, graph, global_var_info,
											 var_info, term->u.branch.kt);
				new_graph = dictionary_union(ss, new_graph, tmp);
				tmp = cps_opt_constant_folding(ss, graph, global_var_info,
											   var_info, term->u.branch.kf);
				return dictionary_union(ss, new_graph, tmp);
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
				new_kont->u.kargs.term->u.cont.k = term->u.branch.kf;
				sly_value tmp =
					cps_opt_constant_folding(ss, graph, global_var_info,
											 var_info, term->u.branch.kf);
				return dictionary_union(ss, new_graph, tmp);
			} else {
				new_kont->u.kargs.term->u.cont.k = term->u.branch.kt;
				sly_value tmp =
					cps_opt_constant_folding(ss, graph, global_var_info,
											 var_info, term->u.branch.kt);
				return dictionary_union(ss, new_graph, tmp);
			}
		}
		sly_assert(0, "Error Unreachable");
	} break;
	case tt_cps_kreceive: {
		sly_value tmp = cps_opt_constant_folding(ss, graph, global_var_info,
												 var_info, kont->u.kreceive.k);
		return dictionary_union(ss, new_graph, tmp);
	} break;
	case tt_cps_kproc: {
		sly_value tmp = cps_opt_constant_folding(ss, graph, global_var_info,
												 var_info, kont->u.kproc.body);
		return dictionary_union(ss, new_graph, tmp);
	} break;
	}
	return new_graph;
}

static sly_value
cps_get_alias(sly_value info, sly_value name)
{
	CPS_Var_Info *vi = GET_PTR(dictionary_ref(info, name, SLY_VOID));
	if (vi && vi->isalias) {
		sly_assert(vi->binding->type == tt_cps_values, "Error expected values");
		return cps_get_alias(info, list_ref(vi->binding->u.values.args,
											vi->which));
	}
	return name;
}

static sly_value
cps_map_aliases(Sly_State *ss, sly_value info, sly_value lst)
{
	if (null_p(lst)) {
		return lst;
	}
	return cons(ss, cps_get_alias(info, car(lst)),
				cps_map_aliases(ss, info, cdr(lst)));
}

static void
cps_opt_resolve_aliases(Sly_State *ss,
						sly_value graph,
						sly_value global_var_info,
						sly_value var_info,
						sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		sly_value info = dictionary_ref(var_info, k, SLY_VOID);
		sly_assert(!void_p(info), "No var info found for continuation");
		if (term->type == tt_cps_continue) {
			CPS_Expr *expr = term->u.cont.expr;
			switch (expr->type) {
			case tt_cps_call: {
				expr->u.call.proc = cps_get_alias(info, expr->u.call.proc);
				expr->u.call.args = cps_map_aliases(ss, info, expr->u.call.args);
			} break;
			case tt_cps_kcall: {
				sly_assert(0, "unimplemented");
			} break;
			case tt_cps_const: break;
			case tt_cps_proc: {
				cps_opt_resolve_aliases(ss, graph, global_var_info,
										var_info, expr->u.proc.k);
			} break;
			case tt_cps_prim: break;
			case tt_cps_primcall: {
				expr->u.primcall.args =
					cps_map_aliases(ss, info, expr->u.primcall.args);
			} break;
			case tt_cps_values: {
				expr->u.values.args =
					cps_map_aliases(ss, info, expr->u.values.args);
			} break;
			case tt_cps_set: {
				expr->u.set.val = cps_get_alias(info, expr->u.set.val);
				expr->u.set.var = cps_get_alias(info, expr->u.set.var);
			} break;
			case tt_cps_fix: {
				sly_value procs = expr->u.fix.procs;
				CPS_Expr *p;
				while (!null_p(procs)) {
					p = GET_PTR(car(procs));
					if (p->type == tt_cps_proc) {
						cps_opt_resolve_aliases(ss, graph, global_var_info,
												var_info, p->u.proc.k);
					}
					procs = cdr(procs);
				}
			} break;
			}
			cps_opt_resolve_aliases(ss, graph, global_var_info,
									var_info, term->u.cont.k);
		} else if (term->type == tt_cps_branch) {
			term->u.branch.arg = cps_get_alias(info, term->u.branch.arg);
			cps_opt_resolve_aliases(ss, graph, global_var_info,
									var_info, term->u.branch.kt);
			cps_opt_resolve_aliases(ss, graph, global_var_info,
										  var_info, term->u.branch.kf);
		} else {
			sly_assert(0, "Unreachable");
		}
	} break;
	case tt_cps_kreceive: {
		cps_opt_resolve_aliases(ss, graph, global_var_info,
								var_info, kont->u.kreceive.k);
	} break;
	case tt_cps_kproc: {
		cps_opt_resolve_aliases(ss, graph, global_var_info,
								var_info, kont->u.kproc.body);
	} break;
	}
}

static sly_value
replace_ktails(Sly_State *ss, sly_value graph,
			   sly_value ktail, sly_value kreplace, sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			term->u.cont.k = replace_ktails(ss, graph, ktail, kreplace,
											term->u.cont.k);
		} else if (term->type == tt_cps_branch) {
			term->u.branch.kt = replace_ktails(ss, graph, ktail, kreplace,
											   term->u.branch.kt);
			term->u.branch.kf = replace_ktails(ss, graph, ktail, kreplace,
											   term->u.branch.kf);
		} else {
			sly_assert(0, "Unreachable");
		}
	} break;
	case tt_cps_kreceive: {
		kont->u.kreceive.k =
			replace_ktails(ss, graph, ktail, kreplace, kont->u.kreceive.k);
	} break;
	case tt_cps_kproc: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_ktail: {
		if (!sly_eq(ktail, k)) {
			printf("expected: ");
			sly_displayln(ktail);
			printf("got: ");
			sly_displayln(k);
		}
		sly_assert(sly_eq(ktail, k), "Error unexpected ktail");
		return kreplace;
	} break;
	}
	return k;
}

static CPS_Kont *
bind_args_to_k(Sly_State *ss, sly_value graph, CPS_Kont *kont,
			   sly_value cc, sly_value args, sly_value vars,
			   struct arity_t arity)
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
			sly_value var = car(vars);
			vars = cdr(vars);
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
			sly_value var = car(vars);
			vars = cdr(vars);
			CPS_Expr *expr = cps_new_expr();
			expr->type = tt_cps_primcall;
			expr->u.primcall.prim = primops[tt_prim_list].name;
			expr->u.primcall.args = args;
			CPS_Term *t = cps_new_term();
			t->type = tt_cps_continue;
			t->u.cont.k = cc;
			CPS_Kont *next = cps_make_kargs(ss, cps_gensym_label_name(ss), t,
											make_list(ss, 1, var));
			cps_graph_set(ss, graph, next->name, next);
			kont->u.kargs.term->u.cont.k = next->name;
			kont->u.kargs.term->u.cont.expr = expr;
			return next;
		}
	} else {
		// pass (car args) => kargs ((car arity)) -> ...
		sly_value val = car(args);
		args = cdr(args);
		sly_value var = car(vars);
		vars = cdr(vars);
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
		return bind_args_to_k(ss, graph, next, cc, args, vars, arity);
	}
	sly_assert(0, "Unreachable");
}

static sly_value cps_alpha_rename(Sly_State *ss, sly_value graph,
								  sly_value new_graph, sly_value k,
								  sly_value replacements);

static sly_value
cps_alpha_rename_get_replacement(sly_value var, sly_value replacements)
{
	sly_value x = dictionary_ref(replacements, var, SLY_VOID);
	if (void_p(x)) {
		return var;
	}
	return x;
}

static sly_value
cps_alpha_rename_map(Sly_State *ss, sly_value replacements, sly_value lst)
{
	if (null_p(lst)) {
		return lst;
	}
	return cons(ss, cps_alpha_rename_get_replacement(car(lst), replacements),
				cps_alpha_rename_map(ss, replacements, cdr(lst)));
}

static void
cps_alpha_rename_visit_expr(Sly_State *ss, sly_value graph, sly_value new_graph,
							CPS_Expr *expr, sly_value replacements)
{
	switch (expr->type) {
	case tt_cps_call: {
		expr->u.call.proc =
			cps_alpha_rename_get_replacement(expr->u.call.proc, replacements);
		expr->u.call.args =
			cps_alpha_rename_map(ss, replacements, expr->u.call.args);
	} break;
	case tt_cps_kcall: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_const: break;
	case tt_cps_proc: {
		expr->u.proc.k = cps_alpha_rename(ss, graph, new_graph,
										  expr->u.proc.k, replacements);
	} break;
	case tt_cps_prim: break;
	case tt_cps_primcall: {
		expr->u.primcall.args =
			cps_alpha_rename_map(ss, replacements, expr->u.primcall.args);
	} break;
	case tt_cps_values: {
		expr->u.values.args =
			cps_alpha_rename_map(ss, replacements, expr->u.values.args);
	} break;
	case tt_cps_set: {
		expr->u.set.val =
			cps_alpha_rename_get_replacement(expr->u.set.val, replacements);
		expr->u.set.var =
			cps_alpha_rename_get_replacement(expr->u.set.var, replacements);
	} break;
	case tt_cps_fix: {
		expr->u.fix.names =
			cps_alpha_rename_map(ss, replacements, expr->u.fix.names);
		sly_value procs = expr->u.fix.procs;
		while (!null_p(procs)) {
			CPS_Expr *proc = GET_PTR(car(procs));
			proc->u.proc.k = cps_alpha_rename(ss, graph, new_graph,
											  proc->u.proc.k, replacements);
			procs = cdr(procs);
		}
	} break;
	default: sly_assert(0, "Unreachable");
	}
}

static sly_value
cps_alpha_rename(Sly_State *ss, sly_value graph, sly_value new_graph,
				 sly_value k, sly_value replacements)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	if (kont->type == tt_cps_ktail) {
		return k;
	}
	kont = cps_deep_copy_kont(kont);
	kont->name = cps_gensym_label_name(ss);
	cps_graph_set(ss, graph, kont->name, kont);
	cps_graph_set(ss, new_graph, kont->name, kont);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		sly_value vars = kont->u.kargs.vars;
		while (!null_p(vars)) {
			sly_value var = car(vars);
			if (void_p(dictionary_ref(replacements, var, SLY_VOID))) {
				dictionary_set(ss, replacements, car(vars),
							   cps_gensym_temporary_name(ss));
			}
			vars = cdr(vars);
		}
		kont->u.kargs.vars =
			cps_alpha_rename_map(ss, replacements, kont->u.kargs.vars);
		if (term->type == tt_cps_continue) {
			cps_alpha_rename_visit_expr(ss, graph, new_graph, term->u.cont.expr,
										replacements);
			term->u.cont.k = cps_alpha_rename(ss, graph, new_graph,
											  term->u.cont.k, replacements);
		} else if (term->type == tt_cps_branch) {
			term->u.branch.arg =
				cps_alpha_rename_get_replacement(term->u.branch.arg,
												 replacements);
			term->u.branch.kt =
				cps_alpha_rename(ss, graph, new_graph,
								 term->u.branch.kt, replacements);
			term->u.branch.kf =
				cps_alpha_rename(ss, graph, new_graph,
								 term->u.branch.kf, replacements);
		} else {
			sly_assert(0, "Unreachable");
		}
	} break;
	case tt_cps_kproc: {
		kont->u.kproc.body = cps_alpha_rename(ss, graph, new_graph,
											  kont->u.kproc.body, replacements);
	} break;
	case tt_cps_kreceive: {
		kont->u.kreceive.k = cps_alpha_rename(ss, graph, new_graph,
											  kont->u.kreceive.k, replacements);
	} break;
	case tt_cps_ktail: break;
	}
	return kont->name;
}

static sly_value
cps_inline_kproc(Sly_State *ss, sly_value graph, sly_value new_graph,
				 CPS_Kont *kont, CPS_Kont *kproc)
{
	CPS_Term *term = kont->u.kargs.term;
	CPS_Expr *expr = term->u.cont.expr;
	sly_value body = cps_alpha_rename(ss, graph, new_graph,
									  kproc->u.kproc.body, make_dictionary(ss));
	sly_value kk = term->u.cont.k;
	sly_value args = expr->u.call.args;
	struct arity_t arity = kproc->u.kproc.arity;
	kont->u.kargs.term = cps_new_term();
	kont->u.kargs.term->type = tt_cps_continue;
	{
		CPS_Kont *krec = cps_graph_ref(graph, kk);
		if (krec->type == tt_cps_kreceive) {
			kk = krec->u.kreceive.k;
		}
	}
	replace_ktails(ss, graph,
				   kproc->u.kproc.tail, kk, body);
	dictionary_set(ss, new_graph, kont->name, (sly_value)kont);
	CPS_Kont *kbody = cps_graph_ref(new_graph, body);
	sly_assert(kbody->type == tt_cps_kargs, "Error expected kargs");
	sly_value vars = kbody->u.kargs.vars;
	kbody->u.kargs.vars = SLY_NULL;
	CPS_Kont *begin = bind_args_to_k(ss, graph, kont, body, args, vars, arity);
	if (begin->u.kargs.term->u.cont.expr == NULL) {
		begin->u.kargs.term = kbody->u.kargs.term;
	}
	return new_graph;
}

static sly_value
cps_opt_beta_contraction(Sly_State *ss,
						 sly_value graph,
						 sly_value global_var_info,
						 sly_value var_info,
						 sly_value k)
{
	// Inlines functions that are only called once.
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
				sly_value tmp =
					cps_opt_beta_contraction(ss, graph, global_var_info,
											 var_info, expr->u.proc.k);
				new_graph = dictionary_union(ss, new_graph, tmp);
			}
			if (expr->type == tt_cps_fix) {
				sly_value procs = expr->u.fix.procs;
				CPS_Expr *p;
				while (!null_p(procs)) {
					p = GET_PTR(car(procs));
					if (p->type == tt_cps_proc) {
						sly_value tmp =
							cps_opt_beta_contraction(ss, graph, global_var_info,
													 var_info, p->u.proc.k);
						new_graph = dictionary_union(ss, new_graph, tmp);
					}
					procs = cdr(procs);
				}
				sly_value tmp =
					cps_opt_beta_contraction(ss, graph, global_var_info,
											 var_info, term->u.cont.k);
				return dictionary_union(ss, new_graph, tmp);
			}
			if (expr->type != tt_cps_call) {
				sly_value tmp =
					cps_opt_beta_contraction(ss, graph, global_var_info,
											 var_info, term->u.cont.k);
				return dictionary_union(ss, new_graph, tmp);
			}
			sly_value info = dictionary_ref(var_info, k, SLY_VOID);
			sly_assert(!void_p(info), "STOP HERE");
			CPS_Var_Info *vi = GET_PTR(dictionary_ref(info, expr->u.call.proc,
													  SLY_VOID));
			CPS_Expr *binding = vi->binding;
			sly_value knext = term->u.cont.k;
			if (binding && binding->type == tt_cps_proc) {
				if (!var_is_used_once(global_var_info, expr->u.call.proc)) {
					sly_value tmp =
						cps_opt_beta_contraction(ss, graph, global_var_info,
												 var_info, term->u.cont.k);
					return dictionary_union(ss, new_graph, tmp);
				}
				CPS_Kont *kproc = cps_graph_ref(graph, binding->u.proc.k);
				CPS_Kont *new_kont = cps_copy_kont(kont);
				CLICK();
				cps_inline_kproc(ss, graph, new_graph, new_kont, kproc);
				knext = new_kont->name;
				return dictionary_union(ss, new_graph, graph);
			}
			sly_value tmp = cps_opt_beta_contraction(ss, graph, global_var_info,
													 var_info, knext);
			return dictionary_union(ss, new_graph, tmp);
		} else if (term->type == tt_cps_branch) {
			sly_value tmp =
				cps_opt_beta_contraction(ss, graph, global_var_info,
										 var_info, term->u.branch.kt);
			new_graph = dictionary_union(ss, new_graph, tmp);
			tmp = cps_opt_beta_contraction(ss, graph, global_var_info,
										   var_info, term->u.branch.kf);
			return dictionary_union(ss, new_graph, tmp);
		}
		sly_assert(0, "Error Unreachable");
	} break;
	case tt_cps_kreceive: {
		sly_value tmp = cps_opt_beta_contraction(ss, graph, global_var_info,
												 var_info, kont->u.kreceive.k);
		return dictionary_union(ss, new_graph, tmp);
	} break;
	case tt_cps_kproc: {
		sly_value tmp = cps_opt_beta_contraction(ss, graph, global_var_info,
												 var_info, kont->u.kproc.body);
		return dictionary_union(ss, new_graph, tmp);
	} break;
	}
	return new_graph;
}

sly_value
cps_opt_contraction_phase(Sly_State *ss, sly_value graph, sly_value k, int debug)
{
	static int called = 0;
	int rounds = 0;
	sly_value gvi, vi;
	called++;
	printf("CONTRACTION PHASE #%d\n", called);
#if 0
	gvi = make_dictionary(ss);
	vi = cps_collect_var_info(ss, graph,
							  gvi,
							  make_dictionary(ss),
							  make_dictionary(ss), NULL, k);
	cps_box_variables(ss, graph, k, gvi);
#endif
	do {
		rounds++;
		printf("ROUND #%d\n", rounds);
		gvi = make_dictionary(ss);
		vi = cps_collect_var_info(ss, graph,
								  gvi,
								  make_dictionary(ss),
								  make_dictionary(ss), NULL, k);
		cps_opt_resolve_aliases(ss, graph, gvi, vi, k);
		if (debug) {
			printf("RESOLVE-ALIASES:\n");
			cps_display(ss, graph, k);
			printf("================================================\n");
		}
		gvi = make_dictionary(ss);
		vi = cps_collect_var_info(ss, graph,
								  gvi,
								  make_dictionary(ss),
								  make_dictionary(ss), NULL, k);
		graph = cps_opt_constant_folding(ss, graph, gvi, vi, k);
		if (debug) {
			printf("CONSTANT-FOLDING:\n");
			cps_display(ss, graph, k);
			printf("================================================\n");
		}
		gvi = make_dictionary(ss);
		vi = cps_collect_var_info(ss, graph,
								  gvi,
								  make_dictionary(ss),
								  make_dictionary(ss), NULL, k);
		graph = cps_opt_beta_contraction(ss, graph, gvi, vi, k);
		if (debug) {
			printf("BETA-CONTRACTION:\n");
			cps_display(ss, graph, k);
			printf("================================================\n");
		}
	} while (CLICK_RESET());
	return graph;
}

#if 0
sly_value
cps_make_tail_calls_explicit(Sly_State *ss, sly_value graph, sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, start);
	sly_value new_graph = make_dictionary(ss);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		switch (term->type) {
		case tt_cps_continue: {
			CPS_Expr *expr = term->u.cont.expr;
			if (expr->type == tt_cps_call) {
				sly_assert(0, "unimplemented");
			} else {
				cps_graph_set(ss, new_graph, k, kont);
				sly_value j = cps_make_tail_calls_explicit(ss, graph, term->u.cont.k);
				return dictionary_union(ss, new_graph, j);
			}
		} break;
		case tt_cps_branch: {
			sly_value t = cps_make_tail_calls_explicit(ss, graph, term->u.branch.kt);
			sly_value f = cps_make_tail_calls_explicit(ss, graph, term->u.branch.kf);
			return dictionary_union(ss, t, f);
		} break;
		default: sly_assert(0, "unreachable");
		}
	} break;
	case tt_cps_kproc: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_kcall: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_kreceive: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_ktail: {
		sly_assert(0, "unimplemented");
	} break;
	default: sly_assert(0, "unreachable");
	}
	return graph;
}
#endif

static sly_value
cps_collect_free_variables_visit_expr(Sly_State *ss, CPS_Expr *expr)
{
	switch (expr->type) {
	case tt_cps_const: break;
	case tt_cps_call: {
		return cons(ss, expr->u.call.proc, expr->u.call.args);
	} break;
	case tt_cps_kcall: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_proc: break;
	case tt_cps_prim: {
		return make_list(ss, 1, expr->u.prim.name);
	} break;
	case tt_cps_primcall: {
		return expr->u.primcall.args;
	} break;
	case tt_cps_values: {
		return expr->u.values.args;
	} break;
	case tt_cps_box: { sly_assert(0, "unimplemented"); } break;
	case tt_cps_unbox: { sly_assert(0, "unimplemented"); } break;
	case tt_cps_set: {
		return make_list(ss, 2,
						 expr->u.set.val,
						 expr->u.set.var);
	} break;
	case tt_cps_fix: break;
	default: sly_assert(0, "unreachable");
	}
	return SLY_NULL;
}

static sly_value
_cps_collect_free_variables(Sly_State *ss, sly_value graph,
						   sly_value var_info,
						   sly_value total_vars,
						   sly_value local_vars,
						   sly_value free_info,
						   sly_value visited,
						   sly_value k)
{
	if (!void_p(dictionary_ref(visited, k, SLY_VOID))) {
		return list_subtract(ss, total_vars, local_vars);
	}
	dictionary_set(ss, visited, k, SLY_TRUE);
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		total_vars = list_union(ss, total_vars, kont->u.kargs.vars);
		local_vars = list_union(ss, local_vars, kont->u.kargs.vars);
		if (term->type == tt_cps_continue) {
			CPS_Expr *expr = term->u.cont.expr;
			total_vars = list_union(ss, total_vars,
									cps_collect_free_variables_visit_expr(ss, expr));
			sly_value free_vars = SLY_NULL;
			sly_value cont_vars = SLY_NULL;
			if (expr->type == tt_cps_call) {
				sly_value info = dictionary_ref(var_info, k, SLY_VOID);
				CPS_Var_Info *vi = GET_PTR(dictionary_ref(info, expr->u.call.proc, SLY_VOID));
				if (vi
					&& vi->binding
					&& vi->binding->type == tt_cps_proc) {
					CPS_Expr *proc = vi->binding;
					sly_value kproc = proc->u.proc.k;
					sly_value kfv = dictionary_ref(free_info, kproc, SLY_VOID);
					if (void_p(kfv)) {
						free_vars = _cps_collect_free_variables(ss, graph,
																var_info,
																SLY_NULL,
																SLY_NULL,
																free_info,
																visited,
																kproc);
						dictionary_set(ss, free_info, kproc, free_vars);
					} else {
						free_vars = list_union(ss, kfv, free_vars);
					}
				}
				cont_vars = _cps_collect_free_variables(ss, graph,
														var_info,
														SLY_NULL,
														SLY_NULL,
														free_info,
														visited,
														term->u.cont.k);
				dictionary_set(ss, free_info, term->u.cont.k, cont_vars);
				return list_union(ss, list_subtract(ss, total_vars, local_vars), cont_vars);
			} else if (expr->type == tt_cps_proc) {
				sly_value kproc = expr->u.proc.k;
				free_vars = _cps_collect_free_variables(ss, graph,
														var_info,
														SLY_NULL,
														SLY_NULL,
														free_info,
														visited,
														kproc);
				dictionary_set(ss, free_info, kproc, free_vars);
			} else if (expr->type == tt_cps_fix) {
				local_vars = list_union(ss, local_vars, expr->u.fix.names);
				sly_value procs = expr->u.fix.procs;
				while (!null_p(procs)) {
					CPS_Expr *expr = GET_PTR(car(procs));
					if (expr->type == tt_cps_proc) {
						sly_value kproc = expr->u.proc.k;
						sly_value tmp = _cps_collect_free_variables(ss, graph,
																	var_info,
																	SLY_NULL,
																	SLY_NULL,
																	free_info,
																	visited,
																	kproc);
						dictionary_set(ss, free_info, kproc, tmp);
						free_vars = list_union(ss, free_vars, tmp);
					}
					procs = cdr(procs);
				}
			}
			if (null_p(cont_vars)) {
				cont_vars = _cps_collect_free_variables(ss, graph,
														var_info,
														total_vars,
														local_vars,
														free_info,
														visited,
														term->u.cont.k);
			}
			free_vars = list_subtract(ss,
									  list_union(ss, free_vars, cont_vars),
									  local_vars);
//			free_vars = list_union(ss, list_subtract(ss, total_vars, local_vars), cont_vars);
			return free_vars;
		} else if (term->type == tt_cps_branch) {
			total_vars = cons(ss, term->u.branch.arg, total_vars);
			return list_union(ss, _cps_collect_free_variables(ss, graph,
															  var_info,
															  total_vars,
															  local_vars,
															  free_info,
															  visited,
															  term->u.branch.kt),
							  _cps_collect_free_variables(ss, graph,
														  var_info,
														  total_vars,
														  local_vars,
														  free_info,
														  visited,
														  term->u.branch.kf));
		} else {
			sly_assert(0, "unreachable");
		}
	} break;
	case tt_cps_kreceive: {
		CPS_Kont *next = cps_graph_ref(graph, kont->u.kreceive.k);
		sly_value free_vars = _cps_collect_free_variables(ss, graph,
														  var_info,
														  total_vars,
														  local_vars,
														  free_info,
														  visited,
														  kont->u.kreceive.k);
		return list_subtract(ss, free_vars, next->u.kargs.vars);
	} break;
	case tt_cps_kproc: {
		CPS_Kont *next = cps_graph_ref(graph, kont->u.kproc.body);
		sly_value free_vars = _cps_collect_free_variables(ss, graph,
														  var_info,
														  SLY_NULL,
														  SLY_NULL,
														  free_info,
														  visited,
														  kont->u.kproc.body);
		return list_subtract(ss, free_vars, next->u.kargs.vars);
	} break;
	case tt_cps_ktail: break;
	default: sly_assert(0, "unreachable");
	}
	return list_subtract(ss, total_vars, local_vars);
}

sly_value
cps_collect_free_variables(Sly_State *ss, sly_value graph,
						   sly_value var_info, sly_value k)
{
	sly_value free_info = make_dictionary(ss);
	sly_value vs = _cps_collect_free_variables(ss, graph,
											   var_info,
											   SLY_NULL,
											   SLY_NULL,
											   free_info,
											   make_dictionary(ss),
											   k);
	dictionary_set(ss, free_info, k, vs);
	return free_info;
}

static sly_value
unwrap_kprocs(Sly_State *ss, sly_value procs)
{
	if (null_p(procs)) {
		return SLY_NULL;
	} else {
		CPS_Expr *p = GET_PTR(car(procs));
		if (p->type == tt_cps_proc) {
			sly_value k = p->u.proc.k;
			sly_value ks = unwrap_kprocs(ss, cdr(procs));
			return cons(ss, k, ks);
		} else {
			return unwrap_kprocs(ss, cdr(procs));
		}
	}
}

static void
add_if_free(Sly_State *ss, sly_value free, sly_value declared, sly_value v)
{
	if (bool_p(v)) return;
	if (!booltoc(dictionary_ref(declared, v, SLY_FALSE))) {
		dictionary_set(ss, free, v, SLY_TRUE);
	}
}

static void
cps_free_vars_in_k_visit_expr(Sly_State *ss, sly_value free,
							  sly_value declared, CPS_Expr *expr)
{
	switch (expr->type) {
	case tt_cps_call: {
		add_if_free(ss, free, declared, expr->u.call.proc);
		sly_value args = expr->u.call.args;
		while (!null_p(args)) {
			add_if_free(ss, free, declared, car(args));
			args = cdr(args);
		}
	} break;
	case tt_cps_kcall: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_const: break;
	case tt_cps_proc: break;
	case tt_cps_prim: break;
	case tt_cps_primcall: {
		sly_value args = expr->u.primcall.args;
		while (!null_p(args)) {
			add_if_free(ss, free, declared, car(args));
			args = cdr(args);
		}
	} break;
	case tt_cps_values: {
		sly_value args = expr->u.values.args;
		while (!null_p(args)) {
			add_if_free(ss, free, declared, car(args));
			args = cdr(args);
		}
	} break;
	case tt_cps_make_record: break;
	case tt_cps_record: {
		sly_value args = expr->u.record.values;
		while (!null_p(args)) {
			add_if_free(ss, free, declared, car(args));
			args = cdr(args);
		}
	} break;
	case tt_cps_record_set: {
		add_if_free(ss, free, declared, expr->u.record_set.record);
		add_if_free(ss, free, declared, expr->u.record_set.val);
	} break;
	case tt_cps_select: {
		add_if_free(ss, free, declared, expr->u.select.record);
	} break;
	case tt_cps_offset: {
		add_if_free(ss, free, declared, expr->u.offset.record);
	} break;
	case tt_cps_code: break;
	case tt_cps_box: {
		add_if_free(ss, free, declared, expr->u.box.val);
	} break;
	case tt_cps_unbox: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_set: {
		add_if_free(ss, free, declared, expr->u.set.var);
		add_if_free(ss, free, declared, expr->u.set.val);
	} break;
	case tt_cps_fix: {
		sly_assert(0, "ERROR should be no more fix expressions at this point");
	} break;
	default: sly_assert(0, "Unreachable");
	}
}

static void
_cps_free_vars_in_k(Sly_State *ss, sly_value graph,
				   sly_value free,
				   sly_value declared,
				   sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	sly_value vars = SLY_NULL;
	switch (kont->type) {
	case tt_cps_kargs: {
		vars = kont->u.kargs.vars;
		while (!null_p(vars)) {
			dictionary_set(ss, declared, car(vars), SLY_TRUE);
			vars = cdr(vars);
		}
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			cps_free_vars_in_k_visit_expr(ss, free, declared, term->u.cont.expr);
			_cps_free_vars_in_k(ss, graph, free, declared, term->u.cont.k);
		} else {
			add_if_free(ss, free, declared, term->u.branch.arg);
			_cps_free_vars_in_k(ss, graph, free, declared, term->u.branch.kt);
			_cps_free_vars_in_k(ss, graph, free, declared, term->u.branch.kf);
		}
	} break;
	case tt_cps_kproc: {
		_cps_free_vars_in_k(ss, graph, free, declared, kont->u.kproc.body);
	} break;
	case tt_cps_kreceive: {
		_cps_free_vars_in_k(ss, graph, free, declared, kont->u.kreceive.k);
	} break;
	case tt_cps_ktail: break;
	}
}

sly_value
cps_free_vars_in_k(Sly_State *ss, sly_value graph, sly_value k)
{
	sly_value free = make_dictionary(ss);
	_cps_free_vars_in_k(ss, graph, free, make_dictionary(ss), k);
	vector *vec = GET_PTR(free);
	sly_value free_vars = SLY_NULL;
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value entry = vec->elems[i];
		if (!slot_is_free(entry)) {
			free_vars = cons(ss, car(entry), free_vars);
		}
	}
	return free_vars;
}

void
cps_display_free_vars_foreach_k(Sly_State *ss, sly_value graph, sly_value k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			cps_display_free_vars_foreach_k(ss, graph, term->u.cont.k);
		} else {
			cps_display_free_vars_foreach_k(ss, graph, term->u.branch.kt);
			cps_display_free_vars_foreach_k(ss, graph, term->u.branch.kf);
		}
	} break;
	case tt_cps_kproc: {
		cps_display_free_vars_foreach_k(ss, graph, kont->u.kproc.body);
	} break;
	case tt_cps_kreceive: {
		cps_display_free_vars_foreach_k(ss, graph, kont->u.kreceive.k);
	} break;
	case tt_cps_ktail: break;
	}
}

static sly_value _cps_display(Sly_State *ss, sly_value graph, sly_value visited, sly_value k);

static sly_value
cps_display_visit_expr(Sly_State *ss, CPS_Expr *expr)
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
		sly_value k = expr->u.proc.k;
		sly_display(k, 1);
		printf(")");
		return make_list(ss, 1, k);
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
	case tt_cps_make_record: {
		printf("(make-record %d)", expr->u.make_record.nfields);
	} break;
	case tt_cps_record: {
		sly_value lst = cons(ss, cstr_to_symbol("record"),
							 expr->u.record.values);
		sly_display(lst, 1);
	} break;
	case tt_cps_select: {
		printf("(select ");
		sly_display(expr->u.select.record, 1);
		printf(" %d)", expr->u.select.field);
	} break;
	case tt_cps_record_set: {
		printf("(record-set! ");
		sly_display(expr->u.record_set.record, 1);
		printf(" %d ", expr->u.record_set.field);
		sly_display(expr->u.record_set.val, 1);
		printf(")");
	} break;
	case tt_cps_offset: {
		printf("(offset ");
		sly_display(expr->u.offset.record, 1);
		printf(" %d)", expr->u.offset.off);
	} break;
	case tt_cps_code: {
		printf("(code ");
		sly_value label = expr->u.code.label;
		sly_display(label, 1);
		printf(")");
		return make_list(ss, 1, label);
	} break;
	case tt_cps_box: {
		printf("(box ");
		sly_display(expr->u.box.val, 1);
		printf(")");
	} break;
	case tt_cps_unbox: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_set: {
		printf("(set! ");
		sly_display(expr->u.set.var, 1);
		printf(" ");
		sly_display(expr->u.set.val, 1);
		printf(")");
	} break;
	case tt_cps_fix: {
		printf("(fix");
		sly_value n, names = expr->u.fix.names;
		sly_value procs = expr->u.fix.procs;
		CPS_Expr *p;
		while (!null_p(names)) {
			printf(" [");
			n = car(names);
			p = GET_PTR(car(procs));
			sly_display(n, 1);
			if (p->type == tt_cps_proc) {
				printf(" (proc ");
				sly_display(p->u.proc.k, 1);
				printf(")");
			} else if (p->type == tt_cps_const) {
				printf(" (const ");
				sly_display(p->u.constant.value, 1);
				printf(")");
			} else if (p->type == tt_cps_box) {
				printf(" (box ");
				sly_display(p->u.box.val, 1);
				printf(")");
			} else {
				printf(" TYPE %d", p->type);
				if (p->type == tt_cps_prim) {
					printf(" ");
					sly_display(p->u.prim.name, 1);
				}
			}
			printf("]");
			names = cdr(names);
			procs = cdr(procs);
		}
		printf(")");
		procs = expr->u.fix.procs;
		return unwrap_kprocs(ss, procs);
	} break;
	default: {
		printf("\ntt = %d\n", expr->type);
		sly_assert(0, "Error not a cps expression");
	}
	}
	return SLY_NULL;
}

static sly_value
_cps_display(Sly_State *ss, sly_value graph, sly_value visited, sly_value k)
{
	if (booltoc(dictionary_ref(visited, k, SLY_FALSE))) {
		return SLY_NULL;
	} else {
		dictionary_set(ss, visited, k, SLY_TRUE);
	}
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_ktail: {
		return SLY_NULL;
	} break;
	case tt_cps_kargs: {
		printf("let ");
		sly_display(kont->name, 1);
		printf(" = kargs ");
		sly_display(kont->u.kargs.vars, 1);
		printf(" -> ");
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			sly_value k = term->u.cont.k;
			CPS_Kont *kont = cps_graph_ref(graph, k);
			sly_display(kont->name, 1);
			printf(" ");
			sly_value l1 = cps_display_visit_expr(ss, term->u.cont.expr);
			printf("\n");
			sly_value l2 = _cps_display(ss, graph, visited, k);
			return list_union(ss, l1, l2);
		} else if (term->type == tt_cps_branch) {
			sly_value kt = term->u.branch.kt;
			sly_value kf = term->u.branch.kf;
			printf("if ");
			sly_display(term->u.branch.arg, 1);
			printf(" then ");
			sly_display(kt, 1);
			printf(" else ");
			sly_display(kf, 1);
			printf("\n");
			sly_value k = _cps_display(ss, graph, visited, kt);
			sly_value j = _cps_display(ss, graph, visited, kf);
			return list_union(ss, k, j);
		}
	} break;
	case tt_cps_kreceive: {
		printf("let ");
		sly_display(kont->name, 1);
		u32 req = list_len(kont->u.kreceive.arity.req);
		char *rst = kont->u.kreceive.arity.rest == SLY_FALSE ? "#f" : "#t";
		printf(" = kreceive (req: %u, rest: %s) => ", req, rst);
		CPS_Kont *tmp = cps_graph_ref(graph, kont->u.kreceive.k);
		sly_display(tmp->name, 1);
		printf("\n");
		k = kont->u.kreceive.k;
	} break;
	case tt_cps_kproc: {
		printf("let ");
		sly_display(kont->name, 1);
		printf(" = λ ");
		sly_value arity = kont->u.kproc.arity.req;
		sly_value rest = kont->u.kproc.arity.rest;
		if (rest != SLY_FALSE) {
			arity = list_append(ss, arity, rest);
		}
		sly_display(arity, 1);
		printf(" -> ");
		sly_display(kont->u.kproc.body, 1);
		printf(" : ");
		sly_display(kont->u.kproc.tail, 1);
		k = kont->u.kproc.body;
		printf("\n");
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
	return _cps_display(ss, graph, visited, k);
}

void
cps_display(Sly_State *ss, sly_value graph, sly_value k)
{
	sly_value visited = make_dictionary(ss);
	sly_value ks = _cps_display(ss, graph, visited, k);
	sly_value lst = ks;
	sly_value js = SLY_NULL;
	do {
		while (!null_p(lst)) {
			k = car(lst);
			js = list_union(ss, _cps_display(ss, graph, visited, k), js);
			lst = cdr(lst);
		}
		lst = js;
		js = SLY_NULL;
	} while (!null_p(lst));
}

void
cps_display_var_info(Sly_State *ss, sly_value var_info)
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
								printf(" = { used = %d, escapes = %d, "
									   "updates = %d, binding = ",
									   info->used, info->escapes,
									   info->updates);
								cps_display_visit_expr(ss, info->binding);
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
