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
#include "intmap.h"

#define CAR(val) (syntax_p(val) ? car(syntax_to_datum(val)) : car(val))
#define CDR(val) (syntax_p(val) ? cdr(syntax_to_datum(val)) : cdr(val))

typedef u32 cps_variable;
typedef u32 cps_label;

enum cps_type {
	tt_cps_const,
	tt_cps_call,
	tt_cps_kcall,
	tt_cps_callwcc,
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

struct arity_t {
	sly_value req;
	sly_value rest;
};

/* continuations */
typedef struct _cps_kargs {
	u32 nvars;
	sly_value *names;
	cps_variable *vars;
	struct _cps_term *term;
} CPS_Kargs;

typedef struct _cps_kreceive {
	struct arity_t arity;
	cps_label k;
} CPS_Kreceive;

typedef struct _cps_Kproc {
	struct arity_t arity;
	cps_label tail;
	cps_label body;
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
	cps_label k;
	struct _cps_expr *expr;
} CPS_Continue;

typedef struct _cps_branch {
	sly_value name;
	cps_variable arg;
	cps_label kt;
	cps_label kf;
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
	u32 nargs;
	sly_value *names; // TODO: Add intmap of variables to names
	cps_variable *args;
} CPS_Values;

typedef struct _cps_const {
	sly_value value;
} CPS_Const;

typedef struct _cps_proc {
	cps_label k;    // kfun
} CPS_Proc;

typedef struct _cps_prim {
	sly_value name;
} CPS_Prim;

typedef struct _cps_primcall {
	sly_value prim_name;
	u32 nargs;
	sly_value *names;
	cps_variable *args;
} CPS_Primcall;

typedef struct _cps_call {
	sly_value proc_name;
	cps_variable proc;
	u32 nargs;
	sly_value *names;
	cps_variable *args;
} CPS_Call;

typedef struct _cps_kcall {
	sly_value proc_name;
	cps_label label;
	u32 nargs;
	sly_value *names;
	cps_variable *args;
} CPS_Kcall;

typedef struct _cps_callwcc {
	sly_value proc_name;
	cps_variable proc;
} CPS_Callwcc;

typedef struct _cps_expr {
	int type;
	union {
		CPS_Call call;
		CPS_Kcall kcall;
		CPS_Callwcc callwcc;
		CPS_Const constant;
		CPS_Proc proc;
		CPS_Prim prim;
		CPS_Primcall primcall;
		CPS_Values values;
	} u;
} CPS_Expr;

struct cps_ret_t {
	intmap *g;
	cps_label k;
};

CPS_Kont *cps_graph_ref(intmap *graph, cps_label k);
intmap *cps_graph_set(intmap *graph, cps_label k, CPS_Kont *kont);
int cps_graph_is_member(intmap *graph, cps_label k);
CPS_Term *cps_new_term(void);
CPS_Expr *cps_make_constant(sly_value value);
CPS_Expr *cps_new_expr(void);
cps_variable cps_new_variable(sly_value name);
cps_variable cps_variable_from_cstr(Sly_State *ss, char *name);
sly_value cps_gensym_temporary_name(Sly_State *ss);
sly_value cps_gensym_label_name(Sly_State *ss);
CPS_Kont *cps_make_kargs(Sly_State *ss, sly_value name, CPS_Term *term, u32 nvars, ...);
CPS_Kont *cps_make_ktail(Sly_State *ss, int genname);
intmap *cps_new_continuation(intmap *graph, cps_label lbl, CPS_Kont **retc);
struct cps_ret_t cps_translate(Sly_State *ss, cps_label cc, intmap *graph, sly_value form);
intmap_list *cps_klist(intmap *graph, cps_label k);
intmap *cps_kgraph(intmap *graph, cps_label k);
void cps_display(intmap *graph, cps_label k);

#define CPS_EXITK 0
#define CPS_RET(g, k) (struct cps_ret_t){g,k}
#define cps_pushback_temporary_name(name)		\
	do {										\
		PB_TNAME = name;						\
	} while (0)

static sly_value PB_TNAME = 0;

CPS_Kont *
cps_graph_ref(intmap *graph, cps_label k)
{
	void *p = intmap_ref(graph, k);
	sly_assert(p != NULL, "Error label not set");
	return p;
}

int
cps_graph_is_member(intmap *graph, cps_label k)
{
	return intmap_ref(graph, k) ? 1 : 0;
}

intmap *
cps_graph_set(intmap *graph, cps_label k, CPS_Kont *kont)
{
	intmap *r = intmap_set(graph, k, kont);
	sly_assert(r != NULL, "Error label already defined");
	return r;
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

cps_variable
cps_new_variable(sly_value name)
{
	u64 hash;
	if (identifier_p(name)) {
		hash = symbol_hash(syntax_to_datum(name));
	} else if (symbol_p(name)) {
		hash = symbol_hash(name);
	} else {
		sly_displayln(name);
		sly_assert(0, "Type error expected symbol");
	}
	union {
		u64 i;
		struct {
			u32 l;
			u32 h;
		} p;
	} z;
	z.i = hash;
	return z.p.l ^ z.p.h;
}

cps_variable
cps_variable_from_cstr(Sly_State *ss, char *name)
{
	return cps_new_variable(make_symbol(ss, name, strlen(name)));
}

sly_value
cps_gensym_temporary_name(Sly_State *ss)
{
	sly_value t;
	if (PB_TNAME) {
		t = PB_TNAME;
		PB_TNAME = 0;
	} else {
		t = gensym_from_cstr(ss, "$t");
	}
	return t;
}

sly_value
cps_gensym_label_name(Sly_State *ss)
{
	return gensym_from_cstr(ss, "$k");
}

CPS_Kont *
cps_make_kargs(Sly_State *ss, sly_value name, CPS_Term *term, u32 nvars, ...)
{
	va_list ap;
	va_start(ap, nvars);
	CPS_Kont *k = cps_make_ktail(ss, 0);
	k->type = tt_cps_kargs;
	k->name = name;
	k->u.kargs.nvars = nvars;
	k->u.kargs.term = term;
	k->u.kargs.names = GC_MALLOC(sizeof(sly_value) * nvars);
	k->u.kargs.vars  = GC_MALLOC(sizeof(cps_variable) * nvars);
	for (u32 i = 0; i < nvars; ++i) {
		sly_value s = va_arg(ap, sly_value);
		k->u.kargs.names[i] = s;
		k->u.kargs.vars[i] = cps_new_variable(s);
	}
	return k;
}

intmap_list *
cps_klist(intmap *graph, cps_label k)
{
	CPS_Kont *kont;
	intmap_list *list = NULL;
	for (;;) {
		kont = cps_graph_ref(graph, k);
		list = intmap_list_append(list,
								  intmap_list_node(k, kont));
		if (kont->type == tt_cps_ktail) {
			return list;
		}
		CPS_Term *term = kont->u.kargs.term;
		switch (term->type) {
		case tt_cps_continue: {
			k = term->u.cont.k;
		} break;
		case tt_cps_branch: {
 			return intmap_list_append(cps_klist(graph, term->u.branch.kt),
									  cps_klist(graph, term->u.branch.kf));
		} break;
		default: sly_assert(0, "Error not a cps term");
		}
	}
	return list;
}

intmap *
cps_kgraph(intmap *graph, cps_label k)
{
	CPS_Kont *kont;
	intmap *new_graph = intmap_empty();
	for (;;) {
		kont = cps_graph_ref(graph, k);
		intmap_set_inplace(new_graph, k, kont);
		if (kont->type == tt_cps_ktail) {
			return new_graph;
		}
		CPS_Term *term = kont->u.kargs.term;
		switch (term->type) {
		case tt_cps_continue: {
			k = term->u.cont.k;
		} break;
		case tt_cps_branch: {
			new_graph = intmap_union(new_graph, cps_kgraph(graph, term->u.branch.kt));
			return intmap_union(new_graph, cps_kgraph(graph, term->u.branch.kf));
		} break;
		default: sly_assert(0, "Error not a cps term");
		}
	}
	return new_graph;
}

#define CPS_CONSTANT()												\
	do {															\
		t = cps_new_term();											\
		t->type = tt_cps_continue;									\
		if (syntax_quote) {											\
			t->u.cont.expr = cps_make_constant(form);				\
		} else {													\
			t->u.cont.expr = cps_make_constant(strip_syntax(form));	\
		}															\
		t->u.cont.k = cc;											\
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, 1,		\
						   cps_gensym_temporary_name(ss));			\
		cc = cps_new_variable(k->name);								\
		graph = cps_graph_set(graph, cc, k);						\
		r.k = cc;													\
		r.g = graph;												\
	} while (0)

struct cps_ret_t
cps_translate(Sly_State *ss, cps_label cc, intmap *graph, sly_value form)
{
	if (null_p(form)) {
		return CPS_RET(graph, cc);
	}
	struct cps_ret_t r;
	int syntax_quote = 0;
	CPS_Term *t;
	CPS_Kont *k;
	if (syntax_pair_p(form) || pair_p(form)) {
		sly_value fst = CAR(form);
		sly_value rest = CDR(form);
		if (identifier_p(fst) || symbol_p(fst)) {
			if (symbol_eq(strip_syntax(fst), make_symbol(ss, "define", 6))
				|| symbol_eq(strip_syntax(fst), make_symbol(ss, "set!", 4))) {
				sly_value name = strip_syntax(CAR(rest));
				sly_value kname = cps_gensym_label_name(ss);
				cps_label kk = cps_new_variable(kname);
				r = cps_translate(ss, kk, graph, CAR(CDR(rest)));
				graph = r.g;
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_values;
				t->u.cont.expr->u.values.nargs = 1;
				t->u.cont.expr->u.values.names = GC_MALLOC(sizeof(sly_value));
				t->u.cont.expr->u.values.args  = GC_MALLOC(sizeof(cps_variable));
				t->u.cont.expr->u.values.names[0] = name;
				t->u.cont.expr->u.values.args[0] = cps_new_variable(name);
				t->u.cont.k = cc;
				k = cps_make_kargs(ss, kname, t, 1, name);
				r.g = cps_graph_set(graph, kk, k);
				return r;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "call/cc", 7))
					   || symbol_eq(strip_syntax(fst),
									make_symbol(ss, "call-with-current-continuation", 30))) {
				sly_value e = CAR(rest);
				sly_value proc_name;
				cps_variable proc;
				k = cps_make_ktail(ss, 0);
				k->type = tt_cps_kreceive;
				k->name = cps_gensym_label_name(ss);
				k->u.kreceive.k = cc;
				k->u.kreceive.arity.req = cons(ss,
											   cps_gensym_temporary_name(ss),
											   SLY_NULL);
				k->u.kreceive.arity.rest = SLY_FALSE;
				cc = cps_new_variable(k->name);
				graph = cps_graph_set(graph, cc, k);
				t = cps_new_term();
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				t->u.cont.expr->type = tt_cps_call;
				t->u.cont.expr->u.call.nargs = 1;
				t->u.cont.expr->u.call.names = GC_MALLOC(sizeof(sly_value));
				t->u.cont.expr->u.call.args  = GC_MALLOC(sizeof(cps_variable));
				t->u.cont.expr->u.call.names[0] = k->name;
				t->u.cont.expr->u.call.args[0] = cc;
				t->u.cont.k = cc;
				sly_value tmp = cps_gensym_temporary_name(ss);
				k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, 1, tmp);
				cc = cps_new_variable(k->name);
				graph = cps_graph_set(graph, cc, k);
				if (identifier_p(e) || symbol_p(e)) {
					proc_name = strip_syntax(e);
					proc = cps_new_variable(proc_name);
				} else {
					r = cps_translate(ss, cc, graph, CAR(rest));
					cc = r.k;
					graph = r.g;
					k = cps_graph_ref(graph, r.k);
					proc_name = tmp;
					proc = cps_new_variable(tmp);
				}
				t->u.cont.expr->u.call.proc_name = proc_name;
				t->u.cont.expr->u.call.proc = proc;
				r.g = graph;
				r.k = cc;
				return r;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "values", 6))) {
				sly_value vlist = SLY_NULL;
				sly_value e;
				t = cps_new_term();
				t->u.cont.k = cc;
				t->type = tt_cps_continue;
				t->u.cont.expr = cps_new_expr();
				sly_value tmp = cps_gensym_temporary_name(ss);
				k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, 1, tmp);
				cc = cps_new_variable(k->name);
				graph = cps_graph_set(graph, cc, k);
				sly_value name;
				while (!null_p(rest)) {
					e = CAR(rest);
					if (identifier_p(e) || symbol_p(e)) {
						name = strip_syntax(e);
					} else { // constant or expression
						r = cps_translate(ss, cc, graph, e);
						cc = r.k;
						graph = r.g;
						k = cps_graph_ref(graph, cc);
						name = tmp;
						tmp = k->u.kargs.names[0];
					}
					vlist = list_append(ss, vlist, cons(ss, name, SLY_NULL));
					rest = CDR(rest);
				}
				t->u.cont.expr->type = tt_cps_values;
				u32 nargs = list_len(vlist);
				t->u.cont.expr->u.values.nargs = nargs;
				t->u.cont.expr->u.values.names = GC_MALLOC(sizeof(sly_value) * nargs);
				t->u.cont.expr->u.values.args  = GC_MALLOC(sizeof(cps_variable) * nargs);
				for (u32 i = 0; i < nargs; ++i) {
					name = car(vlist);
					t->u.cont.expr->u.values.names[i] = name;
					t->u.cont.expr->u.values.args[i] = cps_new_variable(name);
					vlist = cdr(vlist);
				}
				r.g = graph;
				r.k = cc;
				return r;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "call-with-values", 16))) {
				sly_value pe = CAR(rest);
				sly_value re = CAR(CDR(rest));
				cps_label kp;
				sly_value tmp_name = cps_gensym_temporary_name(ss);
				r = cps_translate(ss, cc, graph,
								  make_list(ss, 3,
											make_symbol(ss, "apply", 5),
											re,
											tmp_name));
				graph = r.g;
				cc = r.k;
				r = cps_translate(ss, cc, graph, cons(ss, pe, SLY_NULL));
				graph = r.g;
				cc = r.k;
				kp = cc;
				k = cps_graph_ref(graph, kp);
				k = cps_graph_ref(graph, k->u.kargs.term->u.cont.k);
				k->u.kreceive.arity.req = SLY_NULL;
				k->u.kreceive.arity.rest = tmp_name;
				k = cps_graph_ref(graph, k->u.kreceive.k);
				k->u.kargs.names[0] = tmp_name;
				k->u.kargs.vars[0] = cps_new_variable(tmp_name);
				r.g = graph;
				r.k = cc;
				return r;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "lambda", 6))) {
				sly_value arg_formals = CAR(rest);
				sly_value name = cps_gensym_label_name(ss);
				u32 kp = cps_new_variable(name);
				graph = cps_graph_set(graph, kp, cps_make_ktail(ss, 1));
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
					r = cps_translate(ss, kp, graph, form);
					k->u.kproc.body = r.k;
					graph = r.g;
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
					cps_label kk = cps_new_variable(k->name);
					t->u.cont.expr->u.proc.k = kk;
					graph = cps_graph_set(graph, kk, k);
				}
				{
					sly_value name = cps_gensym_label_name(ss);
					cc = cps_new_variable(name);
					k = cps_make_kargs(ss, name, t, 1,
									   cps_gensym_temporary_name(ss));
					graph = cps_graph_set(graph, cc, k);
				}
				r.k = cc;
				r.g = graph;
				return r;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "begin", 5))) {
				if (!null_p(CDR(rest))) {
					r = cps_translate(ss, cc, graph, cons(ss, fst, CDR(rest)));
					graph = r.g;
					cc = r.k;
				}
				return cps_translate(ss, cc, graph, CAR(rest));
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "if", 2))) {
				sly_value cform = CAR(rest);
				sly_value tform = CAR(CDR(rest));
				sly_value fform = CAR(CDR(CDR(rest)));
				cps_label kt, kf;
				r = cps_translate(ss, cc, graph, tform);
				kt = r.k;
				graph = r.g;
				r = cps_translate(ss, cc, graph, fform);
				kf = r.k;
				graph = r.g;
				t = cps_new_term();
				k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, 1,
								   cps_gensym_temporary_name(ss));
				t->type = tt_cps_branch;
				t->u.branch.arg  = k->u.kargs.vars[0];
				t->u.branch.name = k->u.kargs.names[0];
				t->u.branch.kt = kt;
				t->u.branch.kf = kf;
				cc = cps_new_variable(k->name);
				graph = cps_graph_set(graph, cc, k);
				return cps_translate(ss, cc, graph, cform);
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "quote", 5))) {
				form = CAR(rest);
				CPS_CONSTANT();
				return r;
			} else if (symbol_eq(strip_syntax(fst), make_symbol(ss, "syntax-quote", 12))) {
				syntax_quote = 1;
				form = CAR(rest);
				CPS_CONSTANT();
				return r;
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
		cc = cps_new_variable(k->name);
		graph = cps_graph_set(graph, cc, k);
		t = cps_new_term();
		t->u.cont.k = cc;
		t->type = tt_cps_continue;
		t->u.cont.expr = cps_new_expr();
		sly_value tmp = cps_gensym_temporary_name(ss);
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, 1, tmp);
		cc = cps_new_variable(k->name);
		graph = cps_graph_set(graph, cc, k);
		while (!null_p(form)) {
			e = CAR(form);
			sly_value name;
			if (identifier_p(e) || symbol_p(e)) {
				name = strip_syntax(e);
			} else { // constant or expression
				r = cps_translate(ss, cc, graph, e);
				cc = r.k;
				graph = r.g;
				k = cps_graph_ref(graph, cc);
				name = tmp;
				tmp = k->u.kargs.names[0];
			}
			vlist = list_append(ss, vlist, cons(ss, name, SLY_NULL));
			form = CDR(form);
		}
		sly_value name = car(vlist);
		vlist = cdr(vlist);
		u32 nargs = list_len(vlist);
		t->u.cont.expr->type = tt_cps_call;
		t->u.cont.expr->u.call.nargs = nargs;
		t->u.cont.expr->u.call.names = GC_MALLOC(sizeof(sly_value) * nargs);
		t->u.cont.expr->u.call.args  = GC_MALLOC(sizeof(cps_variable) * nargs);
		t->u.cont.expr->u.call.proc_name = name;
		t->u.cont.expr->u.call.proc = cps_new_variable(name);
		for (u32 i = 0; i < nargs; ++i) {
			name = car(vlist);
			t->u.cont.expr->u.call.names[i] = name;
			t->u.cont.expr->u.call.args[i] = cps_new_variable(name);
			vlist = cdr(vlist);
		}
		r.g = graph;
		r.k = cc;
		return r;
	} else if (identifier_p(form) || symbol_p(form)) {
		t = cps_new_term();
		t->type = tt_cps_continue;
		t->u.cont.expr = cps_new_expr();
		t->u.cont.expr->type = tt_cps_values;
		t->u.cont.expr->u.values.nargs = 1;
		t->u.cont.expr->u.values.names = GC_MALLOC(sizeof(sly_value));
		t->u.cont.expr->u.values.args = GC_MALLOC(sizeof(cps_variable));
		sly_value s = strip_syntax(form);
		t->u.cont.expr->u.values.names[0] = s;
		t->u.cont.expr->u.values.args[0] = cps_new_variable(s);
		t->u.cont.k = cc;
		k = cps_make_kargs(ss, cps_gensym_label_name(ss), t, 1,
						   cps_gensym_temporary_name(ss));
		cc = cps_new_variable(k->name);
		graph = cps_graph_set(graph, cc, k);
		return CPS_RET(graph, cc);
	}
	CPS_CONSTANT();
	return r;
}

static intmap *_cps_display(intmap *graph, intmap *visited, cps_label k);

static intmap *
display_expr(intmap *graph, intmap *visited, CPS_Expr *expr)
{
	switch (expr->type) {
	case tt_cps_call: {
		printf("(call ");
		sly_display(expr->u.call.proc_name, 1);
		u32 nargs = expr->u.call.nargs;
		if (nargs) {
			printf(" ");
			sly_display(expr->u.call.names[0], 1);
			for (u32 i = 1; i < nargs; ++i) {
				printf(" ");
				sly_display(expr->u.call.names[i], 1);
			}
		}
	} break;
	case tt_cps_values: {
		u32 nargs = expr->u.values.nargs;
		printf("(values ");
		sly_display(expr->u.values.names[0], 1);
		for (u32 i = 1; i < nargs; ++i) {
			printf(" ");
			sly_display(expr->u.values.names[i], 1);
		}
	} break;
	case tt_cps_const: {
		printf("(const ");
		sly_display(expr->u.constant.value, 1);
	} break;
	case tt_cps_proc: {
		printf("(proc ");
		CPS_Kont *kont = cps_graph_ref(graph, expr->u.proc.k);
		sly_display(kont->name, 1);
		printf(");\n");
		visited = _cps_display(graph, visited, expr->u.proc.k);
		printf(";");
		return visited;
	} break;
	case tt_cps_callwcc: {
		printf("(call/cc ");
		sly_display(expr->u.callwcc.proc_name, 1);
	} break;
	default: sly_assert(0, "Error not a cps expression");
	}
	printf(")");
	return visited;
}

static struct cps_ret_t
display_term(intmap *graph, intmap *visited, CPS_Term *term)
{
	struct cps_ret_t r = {0};
	switch (term->type) {
	case tt_cps_continue: {
		cps_label k = term->u.cont.k;
		CPS_Kont *kont = cps_graph_ref(graph, k);
		sly_display(kont->name, 1);
		printf(" ");
		visited = display_expr(graph, visited, term->u.cont.expr);
		if (r.g) visited = r.g;
		printf(";\n");
		r.g = visited;
		r.k = k;
	} break;
	case tt_cps_branch: {
		cps_label kt = term->u.branch.kt;
		cps_label kf = term->u.branch.kf;
		printf("if ");
		sly_display(term->u.branch.name, 1);
		printf(" then ");
		CPS_Kont *k = cps_graph_ref(graph, kt);
		sly_display(k->name, 1);
		k = cps_graph_ref(graph, kf);
		printf(" else ");
		sly_display(k->name, 1);
		printf(";\n");
		visited = _cps_display(graph, visited, kt);
		visited = _cps_display(graph, visited, kf);
		r.g = visited;
		r.k = 0;
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
	return r;
}

static intmap *
_cps_display(intmap *graph, intmap *visited, cps_label k)
{
begin:
	if (cps_graph_is_member(visited, k)) {
		return visited;
	}
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_ktail: {
		return visited;
	} break;
	case tt_cps_kargs: {
		printf("let ");
		sly_display(kont->name, 1);
		printf(" = kargs ");
		u32 nvars = kont->u.kargs.nvars;
		if (nvars) {
			sly_display(kont->u.kargs.names[0], 1);
			for (u32 i = 1; i < nvars; ++i) {
				printf(" ");
				sly_display(kont->u.kargs.names[i], 1);
			}
		}
		printf(" -> ");
		struct cps_ret_t r = display_term(graph, visited, kont->u.kargs.term);
		k = r.k;
		visited = r.g;
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
		visited = cps_graph_set(visited, k, kont);
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
		visited = cps_graph_set(visited, k, kont);
		k = kont->u.kproc.body;
		printf(";\n");
		goto begin;
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
	if (k) {
		visited = _cps_display(graph, visited, k);
	}
	if (!cps_graph_is_member(visited, k)) {
		visited = cps_graph_set(visited, k, kont);
	}
	return visited;
}

void
cps_display(intmap *graph, cps_label k)
{
	_cps_display(graph, intmap_empty(), k);
	CPS_Kont *kont = intmap_ref(graph, CPS_EXITK);
	if (kont && kont->type == tt_cps_ktail) {
		printf("let ");
		sly_display(kont->name, 1);
		printf(" = $haltk;\n");
	}
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
				intmap *graph = intmap_empty();
				{
					sly_value name = make_symbol(&ss, "$tkexit", 7);
					CPS_Kont *kont = cps_make_ktail(&ss, 0);
					kont->name = name;
					graph = cps_graph_set(graph, CPS_EXITK, kont);
				}
				sly_displayln(strip_syntax(ast));
				struct cps_ret_t r = cps_translate(&ss, CPS_EXITK, graph, ast);
				cps_display(r.g, r.k);
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
