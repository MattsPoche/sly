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
	tt_cps_values,
	tt_cps_branch,
	tt_cps_continue,
	tt_cps_kargs,
	tt_cps_ktail,
};

/* continuations */
typedef struct _cps_kargs {
	u32 nvars;
	sly_value *names;
	cps_variable *vars;
	struct _cps_term *term;
} CPS_Kargs;

typedef struct _cps_cont {
	int type;
	sly_value name;  // symbol
	union {
		CPS_Kargs kargs;
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
	cps_variable *args;
} CPS_Values;

typedef struct _cps_void {
	sly_value value;
} CPS_Const;

typedef struct _cps_call {
	cps_variable proc;
	u32 nargs;
	cps_variable *args;
} CPS_Call;

typedef struct _cps_expr {
	int type;
	union {
		CPS_Call call;
		CPS_Const constant;
		CPS_Values values;
	} u;
} CPS_Expr;

struct cps_ret_t {
	intmap *g;
	cps_label k;
};

#define CPS_ENTRYK 0
#define CPS_RET(g, k) (struct cps_ret_t){g,k}

CPS_Kont *cps_graph_ref(intmap *graph, cps_label k);
intmap *cps_graph_set(intmap *graph, cps_label k, CPS_Kont *kont);
CPS_Term *cps_new_term(void);
CPS_Expr *cps_make_constant(sly_value value);
CPS_Expr *cps_new_expr(void);
cps_variable cps_new_variable(sly_value name);
CPS_Kont *cps_make_kargs(Sly_State *ss, sly_value name, CPS_Term *term, u32 nvars, ...);
CPS_Kont *cps_make_ktail(Sly_State *ss, int genname);
intmap *cps_new_continuation(intmap *graph, cps_label lbl, CPS_Kont **retc);
struct cps_ret_t cps_translate(Sly_State *ss, cps_label cc, intmap *graph, sly_value form);
intmap_list *cps_klist(intmap *graph, cps_label k);
intmap *cps_kgraph(intmap *graph, cps_label k);
void cps_display(intmap *graph, cps_label k);

CPS_Kont *
cps_graph_ref(intmap *graph, cps_label k)
{
	void *p = intmap_ref(graph, k);
	sly_assert(p != NULL, "Error label not set");
	return p;
}

intmap *
cps_graph_set(intmap *graph, cps_label k, CPS_Kont *kont)
{
	intmap *r = intmap_set(graph, k, kont);
	sly_assert(r != NULL, "Error label already defined");
	return r;
}

#if 0
static DEF_INTMAP_ITER_CB(im_len, p, i,
{
	UNUSED(p);
	*((size_t *)i) += 1;
	printf("i = %zu\n", *((size_t *)i));
	return NULL;
})

size_t
cps_graph_count_nodes(intmap *graph)
{
	size_t i = 0;
	intmap_foreach(graph, 0, im_len, &i);
	return i;
}
#endif

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
		k->u.kargs.names[0] = s;
		k->u.kargs.vars[0] = cps_new_variable(s);
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
		if (identifier_p(fst)) {
			if (symbol_eq(syntax_to_datum(fst), make_symbol(ss, "begin", 5))) {
				if (!null_p(CDR(rest))) {
					r = cps_translate(ss, cc, graph, cons(ss, fst, CDR(rest)));
					graph = r.g;
					cc = r.k;
				}
				return cps_translate(ss, cc, graph, CAR(rest));
			} else if (symbol_eq(syntax_to_datum(fst), make_symbol(ss, "if", 2))) {
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
				k = cps_make_kargs(ss, gensym_from_cstr(ss, "$k"), t, 1,
								   gensym_from_cstr(ss, "$t"));
				t->type = tt_cps_branch;
				t->u.branch.arg  = k->u.kargs.vars[0];
				t->u.branch.name = k->u.kargs.names[0];
				t->u.branch.kt = kt;
				t->u.branch.kf = kf;
				cc = cps_new_variable(k->name);
				graph = cps_graph_set(graph, cc, k);
				return cps_translate(ss, cc, graph, cform);
			} else if (symbol_eq(syntax_to_datum(fst), make_symbol(ss, "quote", 5))) {
				form = CAR(rest);
				goto constant;
			} else if (symbol_eq(syntax_to_datum(fst), make_symbol(ss, "syntax-quote", 12))) {
				syntax_quote = 1;
				form = CAR(rest);
				goto constant;
			} else {
				sly_assert(0, "unimplemented");
			}
		} else {
			sly_assert(0, "unimplemented");
		}
	}
constant:
	t = cps_new_term();
	t->type = tt_cps_continue;
	if (syntax_quote) {
		t->u.cont.expr = cps_make_constant(form);
	} else {
		t->u.cont.expr = cps_make_constant(strip_syntax(form));
	}
	t->u.cont.k = cc;
	k = cps_make_kargs(ss, gensym_from_cstr(ss, "$k"), t, 1,
					   gensym_from_cstr(ss, "$t"));
	cc = cps_new_variable(k->name);
	graph = cps_graph_set(graph, cc, k);
	return CPS_RET(graph, cc);
}

static void
display_expr(intmap *graph, CPS_Expr *expr)
{
	UNUSED(graph);
	switch (expr->type) {
	case tt_cps_call: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_values: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_const: {
		printf("(const ");
		sly_display(expr->u.constant.value, 1);
		printf(")");
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
}

static cps_label
display_term(intmap *graph, CPS_Term *term)
{
	switch (term->type) {
	case tt_cps_continue: {
		cps_label k = term->u.cont.k;
		CPS_Kont *kont = cps_graph_ref(graph, k);
		printf("(continue ");
		sly_display(kont->name, 1);
		printf(" ");
		display_expr(graph, term->u.cont.expr);
		printf(")");
		return k;
	} break;
	case tt_cps_branch: {
		printf("(branch (");
		sly_display(term->u.branch.name, 1);
		printf(") ");
		CPS_Kont *k = cps_graph_ref(graph, term->u.branch.kt);
		sly_display(k->name, 1);
		k = cps_graph_ref(graph, term->u.branch.kf);
		printf(" ");
		sly_display(k->name, 1);
		printf(")\n");
		cps_label kt = term->u.branch.kt;
		cps_label kf = term->u.branch.kf;
		cps_display(graph, kt);
		cps_display(graph, kf);
		intmap_list *l = intmap_to_list(intmap_intersect(cps_kgraph(graph, kt),
														 cps_kgraph(graph, kf)));
		while (l) {
			k = l->p.value;
			printf("$k = ");
			sly_displayln(k->name);
			l = l->next;
		}
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
	return 0;
}

void
cps_display(intmap *graph, cps_label k)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_ktail: {
		printf("ktail");
	} break;
	case tt_cps_kargs: {
		sly_display(kont->name, 1);
		printf(":\t(kargs (");
		u32 nvars = kont->u.kargs.nvars;
		if (nvars) {
			sly_display(kont->u.kargs.names[0], 1);
			for (u32 i = 1; i < nvars; ++i) {
				printf(" ");
				sly_display(kont->u.kargs.names[i], 1);
			}
		}
		printf(")\n\t\t\t");
		k = display_term(graph, kont->u.kargs.term);
		printf(")\n");
	} break;
	default: sly_assert(0, "Error not a cps continuation");
	}
	if (k) {
		cps_display(graph, k);
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
				graph = cps_graph_set(graph, CPS_ENTRYK, cps_make_ktail(&ss, 1));
				struct cps_ret_t r = cps_translate(&ss, CPS_ENTRYK, graph, ast);
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
