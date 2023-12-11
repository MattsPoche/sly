#include <math.h>
#include <stdio.h>
#include <gc.h>
#include "sly_types.h"
#include "cps.h"
#include "cbackend.h"

static const char closure_tmpl[] = "Closure *%s%s(Closure *self) // %s\n{\n";
static const char chk_args_tmpl[] = "\tassert(chk_args(%lu, %d));\n";
static const char push_tmpl[] = "\tpush_arg(%s%s);\n";
static const char pop_tmpl[] = "\t%s%s = pop_arg();\n";
static const char make_closure_tmpl[] = "\tscm_value %s = make_closure();\n";
static const char tail_call_tmpl[] = "\tTAIL_CALL(%s);\n";
static const char main_tmpl[] =
	"int main(void)\n"
	"{\n"
	"\tpush_arg((scm_value)%s);\n"
	"\ttrampoline(GET_PTR(make_closure()));\n"
	"\treturn 0;\n"
	"}\n";


static void
emit_c_push_list(sly_value name, sly_value lst, FILE *file)
{
	if (null_p(lst)) {
		return;
	}
	sly_value elem = car(lst);
	emit_c_push_list(name, cdr(lst), file);
	if (!sly_eq(name, elem)) {
		fprintf(file, push_tmpl, "", symbol_to_cstr(elem));
	}
}

static void
emit_c_pop_list(sly_value lst, FILE *file)
{
	if (!pair_p(lst)) {
		return;
	}
	sly_value elem = car(lst);
	emit_c_pop_list(cdr(lst), file);
	fprintf(file, pop_tmpl, "scm_value ", symbol_to_cstr(elem));
}

static void
emit_c_unpack_self(sly_value lst, int i, FILE *file)
{
	if (null_p(lst)) {
		return;
	}
	sly_value elem = car(lst);
	emit_c_unpack_self(cdr(lst), i + 1, file);
	fprintf(file, "\tscm_value %s = self->free_vars[%d];\n", symbol_to_cstr(elem), i);
}

static char *
emit_c_visit_expr(CPS_Expr *expr, sly_value graph, sly_value free_vars, FILE *file)
{
	static char buf[0xff];
	switch (expr->type) {
	case tt_cps_call: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_kcall: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_const: {
		sly_value value = expr->u.constant.value;
		if (int_p(value)) {
			snprintf(buf, sizeof(buf), "make_int(%ld)", get_int(value));
			return buf;
		} else if (float_p(value)) {
			union {
				f64 f;
				u64 u;
			} f2u;
			f2u.f = get_float(value);
			snprintf(buf, sizeof(buf), "0x%lx", f2u.u);
			return buf;
		} else {
			sly_assert(0, "unimplemented");
		}
	} break;
	case tt_cps_proc: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_prim: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_primcall: {
		emit_c_push_list(SLY_VOID, expr->u.primcall.args, file);
		switch (primop_p(expr->u.primcall.prim)) {
		case tt_prim_void: sly_assert(0, "unimplemented"); break;
		case tt_prim_add: {
			return "primop_add()";
		} break;
		case tt_prim_sub: {
			return "primop_sub()";
		} break;
		case tt_prim_mul: sly_assert(0, "unimplemented"); break;
		case tt_prim_div: sly_assert(0, "unimplemented"); break;
		case tt_prim_idiv: sly_assert(0, "unimplemented"); break;
		case tt_prim_mod: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_and: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_ior: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_xor: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_eqv: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_nor: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_nand: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_not: sly_assert(0, "unimplemented"); break;
		case tt_prim_bw_shift: sly_assert(0, "unimplemented"); break;
		case tt_prim_eq: sly_assert(0, "unimplemented"); break;
		case tt_prim_eqv: sly_assert(0, "unimplemented"); break;
		case tt_prim_equal: sly_assert(0, "unimplemented"); break;
		case tt_prim_num_eq: sly_assert(0, "unimplemented"); break;
		case tt_prim_less: {
			return "primop_less()";
		} break;
		case tt_prim_gr: sly_assert(0, "unimplemented"); break;
		case tt_prim_leq: sly_assert(0, "unimplemented"); break;
		case tt_prim_geq: sly_assert(0, "unimplemented"); break;
		case tt_prim_apply: sly_assert(0, "unimplemented"); break;
		case tt_prim_cons: sly_assert(0, "unimplemented"); break;
		case tt_prim_car: sly_assert(0, "unimplemented"); break;
		case tt_prim_cdr: sly_assert(0, "unimplemented"); break;
		case tt_prim_list: sly_assert(0, "unimplemented"); break;
		case tt_prim_vector: sly_assert(0, "unimplemented"); break;
		case tt_prim_vector_ref: sly_assert(0, "unimplemented"); break;
		case tt_prim_vector_set: sly_assert(0, "unimplemented"); break;
		default: sly_assert(0, "invalid primop");
		}
	} break;
	case tt_cps_values: {
		emit_c_push_list(SLY_VOID, expr->u.values.args, file);
		return NULL;
	} break;
	case tt_cps_make_record: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_record: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_record_set: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_select: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_offset: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_code: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_box: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_unbox: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_set: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_fix: {
		sly_value names = expr->u.fix.names;
		sly_value procs = expr->u.fix.procs;
		while (!null_p(procs)) {
			CPS_Expr *p = GET_PTR(car(procs));
			sly_value name = car(names);
			if (p->type == tt_cps_proc) {
				sly_value code = p->u.proc.k;
				CPS_Kont *kproc = cps_graph_ref(graph, p->u.proc.k);
				kproc->u.kproc.binding = name;
				sly_value vars = dictionary_ref(free_vars, code, SLY_NULL);
				emit_c_push_list(name, vars, file);
				fprintf(file, push_tmpl, "(scm_value)", symbol_to_cstr(code));
				fprintf(file, make_closure_tmpl, symbol_to_cstr(name));
			} else {
				char *tmpl = emit_c_visit_expr(p, graph, free_vars, file);
				fprintf(file, "\tscm_value %s = %s;\n",
						symbol_to_cstr(name), tmpl);
			}
			procs = cdr(procs);
			names = cdr(names);
		}
		return NULL;
	} break;
	default: sly_assert(0, "Unreachable");
	}
	return NULL;
}

static void
_cps_emit_c(Sly_State *ss, sly_value graph, sly_value k, sly_value free_vars, FILE *file)
{
	CPS_Kont *kont = cps_graph_ref(graph, k);
	switch (kont->type) {
	case tt_cps_kargs: {
		CPS_Term *term = kont->u.kargs.term;
		if (term->type == tt_cps_continue) {
			CPS_Expr *expr = term->u.cont.expr;
			CPS_Kont *next = cps_graph_ref(graph, term->u.cont.k);
			if (expr->type == tt_cps_call) {
				if (next->type != tt_cps_ktail) {
					sly_value vars = dictionary_ref(free_vars, next->name, SLY_NULL);
					emit_c_push_list(SLY_VOID, vars, file);
					fprintf(file, push_tmpl, "", "k");
					fprintf(file, push_tmpl, "(scm_value)", symbol_to_cstr(next->name));
					fprintf(file, "\tk = make_closure();\n");
				}
				emit_c_push_list(SLY_VOID, expr->u.call.args, file);
				fprintf(file, push_tmpl, "", "k");
				fprintf(file, tail_call_tmpl, symbol_to_cstr(expr->u.call.proc));
				return;
			} else if (expr->type == tt_cps_primcall) {
				char *tmpl = emit_c_visit_expr(expr, graph, free_vars, file);
				sly_assert(list_len(next->u.kreceive.arity.req) == 1, "Arity mismatch");
				next = cps_graph_ref(graph, next->u.kreceive.k);
				sly_value binding = car(next->u.kargs.vars);
				fprintf(file, "\tscm_value %s = %s;\n", symbol_to_cstr(binding), tmpl);
				_cps_emit_c(ss, graph, next->name, free_vars, file);
				return;
			} else {
				char *tmpl = emit_c_visit_expr(expr, graph, free_vars, file);
				sly_value binding = next->u.kargs.vars;
				if (tmpl) {
					fprintf(file, "\tscm_value %s = %s;\n",
							symbol_to_cstr(car(binding)),
							tmpl);
				}
			}
			_cps_emit_c(ss, graph, term->u.cont.k, free_vars, file);
		} else if (term->type == tt_cps_branch) {
			fprintf(file, "\tif (%s != SCM_FALSE) {\n", symbol_to_cstr(term->u.branch.arg));
			_cps_emit_c(ss, graph, term->u.branch.kt, free_vars, file);
			fprintf(file, "\t} else {\n");
			_cps_emit_c(ss, graph, term->u.branch.kf, free_vars, file);
			fprintf(file, "\t}\n");
		}
	} break;
	case tt_cps_kproc: {
		fprintf(file, closure_tmpl, "", symbol_to_cstr(k),
				symbol_to_cstr(symbol_get_alias(kont->u.kproc.binding)));
		struct arity_t arity = kont->u.kproc.arity;
		fprintf(file, chk_args_tmpl, list_len(arity.req) + 1, booltoc(arity.rest));
		fprintf(file, pop_tmpl, "scm_value ", "k");
		CPS_Kont *next = cps_graph_ref(graph, kont->u.kproc.body);
		sly_value vars = copy_list(ss, next->u.kargs.vars);
		if (booltoc(arity.rest)) {
			vars = list_reverse(ss, vars);
			sly_value rest = car(vars);
			vars = list_reverse(ss, cdr(vars));
			fprintf(file, "\tscm_value %s = cons_rest(%zu);\n",
					symbol_to_cstr(rest), list_len(arity.req));
		}
		emit_c_pop_list(vars, file);
		vars = dictionary_ref(free_vars, k, SLY_NULL);
		if (list_member(kont->u.kproc.binding, vars)) {
			vars = list_remove(ss, vars, kont->u.kproc.binding);
			fprintf(file, "\tscm_value %s = (NB_CLOSURE << 48)|((scm_value)self);\n",
					symbol_to_cstr(kont->u.kproc.binding));
		}
		emit_c_unpack_self(vars, 0, file);
		_cps_emit_c(ss, graph, next->name, free_vars, file);
	} break;
	case tt_cps_kreceive: {
		fprintf(file, closure_tmpl, "", symbol_to_cstr(k), "");
		struct arity_t arity = kont->u.kreceive.arity;
		fprintf(file, chk_args_tmpl, list_len(arity.req), booltoc(arity.rest));
		sly_assert(!booltoc(arity.rest), "unimplemented");
		CPS_Kont *next = cps_graph_ref(graph, kont->u.kreceive.k);
		emit_c_pop_list(next->u.kargs.vars, file);
		sly_value vars = dictionary_ref(free_vars, k, SLY_NULL);
		fprintf(file, "\tscm_value k = self->free_vars[0];\n");
		emit_c_unpack_self(vars, 1, file);
		_cps_emit_c(ss, graph, next->name, free_vars, file);
	} break;
	case tt_cps_ktail: {
		fprintf(file, tail_call_tmpl, "k");
	} break;
	default: sly_assert(0, "Unreachable");
	}
}

void
cps_emit_c(Sly_State *ss, sly_value graph, sly_value start, sly_value free_vars, FILE *file)
{
	fprintf(file, "#include \"sly_prelude.h\"\n\n");
	vector *vec = GET_PTR(free_vars);
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value entry = vec->elems[i];
		if (!slot_is_free(entry)) {
			sly_value k = car(entry);
			CPS_Kont *kont = cps_graph_ref(graph, k);
			if (kont->type != tt_cps_ktail) {
				fprintf(file, "Closure *%s(Closure *self);\n", symbol_to_cstr(k));
			}
		}
	}
	fprintf(file, "\n");
	fprintf(file, closure_tmpl, "", symbol_to_cstr(start), "entry");
	fprintf(file, chk_args_tmpl, 1LU, 0);
	fprintf(file, pop_tmpl, "scm_value ", "k");
	_cps_emit_c(ss, graph, start, free_vars, file);
	fprintf(file, "}\n\n");
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value entry = vec->elems[i];
		if (!slot_is_free(entry) && !sly_equal(car(entry), start)) {
			sly_value k = car(entry);
			CPS_Kont *kont = cps_graph_ref(graph, k);
			if (kont->type != tt_cps_ktail) {
				_cps_emit_c(ss, graph, car(entry), free_vars, file);
				fprintf(file, "}\n\n");
			}
		}
	}
	fprintf(file, main_tmpl, symbol_to_cstr(start));
}
