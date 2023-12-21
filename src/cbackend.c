#include <math.h>
#include <stdio.h>
#include <gc.h>
#include <ctype.h>
#include "sly_types.h"
#include "cps.h"
#include "cbackend.h"

static const char closure_tmpl[] = "scm_value %s%s(scm_value self) // %s\n{\n";
static const char chk_args_tmpl[] =
	"\tscm_assert(chk_args(%lu, %d), \"arity error\");\n";
static const char push_tmpl[] = "\tpush_arg(%s%s);\n";
static const char pop_tmpl[] = "\t%s%s = pop_arg();\n";
static const char tail_call_tmpl[] = "\tTAIL_CALL(%s);\n";
static const char init_top_level_tmpl[] =
	"scm_value load_dynamic(void)\n"
	"{\n"
	"\tinterned = scm_intern_constants(constants, ARR_LEN(constants));\n"
	"\tpush_arg((scm_value)%s);\n"
	"\treturn make_closure();\n"
	"}\n";
static const char main_tmpl[] =
	"int main(void)\n"
	"{\n"
	"\tscm_heap_init();\n"
	"\ttrampoline(load_dynamic());\n"
	"\treturn 0;\n"
	"}\n";

static size_t const_idx = 0;

static char *
intern_constant(Sly_State *ss, sly_value constants, sly_value value)
{
	static char buf[0xff];
	if (int_p(value)) {
		snprintf(buf, sizeof(buf), "make_int(%ld)", get_int(value));
		return buf;
	}
	if (float_p(value)) {
		union {f64 f; u64 u;} f2u;
		f2u.f = get_float(value);
		snprintf(buf, sizeof(buf), "0x%lx", f2u.u);
		return buf;
	}
	if (true_p(value)) {
		return "SCM_TRUE";
	}
	if (false_p(value)) {
		return "SCM_FALSE";
	}
	if (null_p(value)) {
		return "SCM_NULL";
	}
	if (void_p(value)) {
		return "SCM_VOID";
	}
	if (byte_p(value)) {
		snprintf(buf, sizeof(buf), "make_char(%d)", get_byte(value));
		return buf;
	}
	size_t idx;
	sly_value entry = dictionary_entry_ref(constants, value);
	if (slot_is_free(entry)) {
		idx = const_idx++;
		if (pair_p(value)) {
			sly_value lst = value;
			while (pair_p(lst)) {
				intern_constant(ss, constants, car(lst));
				lst = cdr(lst);
			}
			intern_constant(ss, constants, lst);
		} else if (vector_p(value)) {
			vector *vec = GET_PTR(value);
			for(size_t i = 0; i < vec->len; ++i) {
				intern_constant(ss, constants, vec->elems[i]);
			}
		}
		printf("(intern_constant) value = ");
		sly_displayln(value);
		dictionary_set(ss, constants, value, (sly_value)idx);
	} else {
		idx = (size_t)cdr(entry);
	}
	snprintf(buf, sizeof(buf), "interned[%zu]", idx);
	return buf;
}

static char *
convert_to_cid(char *str)
{
	for (size_t i = 0; str[i]; ++i) {
		if (!(isalnum(str[i]) || str[i] == '_')) {
			str[i] = '_';
		}
	}

	return str;
}

static char *
symbol_to_cid(sly_value s)
{
	return convert_to_cid(symbol_to_cstr(s));
}

static void
emit_c_push_list(sly_value name, sly_value lst, FILE *file)
{
	if (null_p(lst)) {
		return;
	}
	sly_value elem = car(lst);
	emit_c_push_list(name, cdr(lst), file);
	if (!sly_eq(name, elem)) {
		fprintf(file, push_tmpl, "", symbol_to_cid(elem));
	}
}

static void
emit_c_push_refs(sly_value name, sly_value lst, FILE *file)
{
	if (null_p(lst)) {
		return;
	}
	sly_value elem = car(lst);
	emit_c_push_list(name, cdr(lst), file);
	if (!sly_eq(name, elem)) {
		fprintf(file, "\tpush_ref(%s);\n", symbol_to_cid(elem));
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
	fprintf(file, pop_tmpl, "scm_value ", symbol_to_cid(elem));
}

static void
emit_c_unpack_self(sly_value lst, int i, FILE *file)
{
	if (null_p(lst)) {
		return;
	}
	sly_value elem = car(lst);
	emit_c_unpack_self(cdr(lst), i + 1, file);
	fprintf(file, "\tscm_value %s = closure_ref(self, %d);\n",
			symbol_to_cid(elem), i);
}

static char *
emit_c_visit_expr(Sly_State *ss, CPS_Expr *expr, sly_value graph,
				  sly_value free_vars, sly_value constants, FILE *file)
{
	switch (expr->type) {
	case tt_cps_call: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_kcall: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_const: {
		sly_value value = expr->u.constant.value;
		return intern_constant(ss, constants, value);
	} break;
	case tt_cps_proc: {
		sly_value code = expr->u.proc.k;
		sly_value vars = dictionary_ref(free_vars, code, SLY_NULL);
		emit_c_push_list(SLY_VOID, vars, file);
		fprintf(file, push_tmpl, "(scm_value)", symbol_to_cid(code));
		return "make_closure()";
	} break;
	case tt_cps_prim: {
		sly_assert(0, "unimplemented");
	} break;
	case tt_cps_primcall: {
		sly_value args = expr->u.primcall.args;
		emit_c_push_refs(SLY_VOID, args, file);
		switch (primop_p(expr->u.primcall.prim)) {
		case tt_prim_void: sly_assert(0, "unimplemented"); break;
		case tt_prim_add: {
			return "primop_add()";
		} break;
		case tt_prim_sub: {
			return "primop_sub()";
		} break;
		case tt_prim_mul: {
			return "primop_mul()";
		} break;
		case tt_prim_div: {
			return "primop_div()";
		} break;
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
		case tt_prim_cons: {
			return "primop_cons()";
		} break;
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
		sly_value args = expr->u.values.args;
		while (!null_p(args)) {
			fprintf(file, "\tpush_ref(%s);\n", symbol_to_cid(car(args)));
			args = cdr(args);
		}
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
		fprintf(file, "\tbox_set(%s, %s);\n",
				symbol_to_cid(expr->u.set.var),
				symbol_to_cid(expr->u.set.val));
		return NULL;
	} break;
	case tt_cps_fix: {
		sly_value names = expr->u.fix.names;
		sly_value procs = expr->u.fix.procs;
		while (!null_p(names)) {
			sly_value name = car(names);
			fprintf(file, "\tscm_value %s = make_box();\n", symbol_to_cid(name));
			names = cdr(names);
		}
		names = expr->u.fix.names;
		while (!null_p(procs)) {
			CPS_Expr *p = GET_PTR(car(procs));
			sly_value name = car(names);
			if (p->type == tt_cps_proc) {
				sly_value code = p->u.proc.k;
				CPS_Kont *kproc = cps_graph_ref(graph, code);
				kproc->u.kproc.binding = name;
				sly_value vars = dictionary_ref(free_vars, code, SLY_NULL);
				emit_c_push_list(name, vars, file);
				fprintf(file, push_tmpl, "(scm_value)", symbol_to_cid(code));
				fprintf(file, "\tbox_set(%s, make_closure());\n", symbol_to_cid(name));
			} else {
				char *tmpl = emit_c_visit_expr(ss, p, graph, free_vars, constants, file);
				fprintf(file, "\tbox_set(%s, %s);\n", symbol_to_cid(name), tmpl);
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
_cps_emit_c(Sly_State *ss, sly_value graph, sly_value k,
			sly_value free_vars, sly_value constants, FILE *file)
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
					emit_c_push_refs(SLY_VOID, vars, file);
					fprintf(file, push_tmpl, "", "k");
					fprintf(file, push_tmpl, "(scm_value)", symbol_to_cid(next->name));
					fprintf(file, "\tk = make_closure();\n");
				}
				printf("k = ");
				sly_displayln(k);
				emit_c_push_refs(SLY_VOID, expr->u.call.args, file);
				fprintf(file, push_tmpl, "", "k");
				fprintf(file, tail_call_tmpl, symbol_to_cid(expr->u.call.proc));
				return;
			} else if (expr->type == tt_cps_primcall) {
				char *tmpl = emit_c_visit_expr(ss, expr, graph, free_vars, constants, file);
				sly_assert(list_len(next->u.kreceive.arity.req) == 1, "Arity mismatch");
				next = cps_graph_ref(graph, next->u.kreceive.k);
				sly_value binding = car(next->u.kargs.vars);
				fprintf(file, "\tscm_value %s = %s;\n", symbol_to_cid(binding), tmpl);
				_cps_emit_c(ss, graph, next->name, free_vars, constants, file);
				return;
			} else {
				char *tmpl = emit_c_visit_expr(ss, expr, graph, free_vars, constants, file);
				if (tmpl) {
					if (next->type == tt_cps_ktail) {
						fprintf(file, push_tmpl, "", tmpl);
					} else {
						sly_value binding = next->u.kargs.vars;
						fprintf(file, "\tscm_value %s = %s;\n",
								symbol_to_cid(car(binding)), tmpl);
					}
				}
			}
			_cps_emit_c(ss, graph, term->u.cont.k, free_vars, constants, file);
		} else if (term->type == tt_cps_branch) {
			fprintf(file, "\tif (%s != SCM_FALSE) {\n", symbol_to_cid(term->u.branch.arg));
			_cps_emit_c(ss, graph, term->u.branch.kt, free_vars, constants, file);
			fprintf(file, "\t} else {\n");
			_cps_emit_c(ss, graph, term->u.branch.kf, free_vars, constants, file);
			fprintf(file, "\t}\n");
		}
	} break;
	case tt_cps_kproc: {
		sly_value name = symbol_p(kont->u.kproc.binding)
			? symbol_get_alias(kont->u.kproc.binding) : kont->name;
		fprintf(file, closure_tmpl, "", symbol_to_cid(k), symbol_to_cstr(name));
		fprintf(file, "\tscm_chk_heap(&self);\n");
		struct arity_t arity = kont->u.kproc.arity;
		fprintf(file, chk_args_tmpl, list_len(arity.req) + 1, booltoc(arity.rest));
		fprintf(file, pop_tmpl, "scm_value ", "k");
		CPS_Kont *next = cps_graph_ref(graph, kont->u.kproc.body);
		sly_value vars = copy_list(ss, next->u.kargs.vars);
		sly_value v = vars;
		while (!null_p(v)) {
			fprintf(file, "\tscm_value %s = make_box();\n", symbol_to_cid(car(v)));
			v = cdr(v);
		}
		if (booltoc(arity.rest)) {
			vars = list_reverse(ss, vars);
			sly_value rest = car(vars);
			vars = list_reverse(ss, cdr(vars));
			while (!null_p(vars)) {
				fprintf(file, "\tbox_set(%s, pop_arg());\n",
						symbol_to_cid(car(vars)));
				vars = cdr(vars);
			}
			fprintf(file, "\tbox_set(%s, cons_rest());\n", symbol_to_cid(rest));
		} else {
			while (!null_p(vars)) {
				fprintf(file, "\tbox_set(%s, pop_arg());\n",
						symbol_to_cid(car(vars)));
				vars = cdr(vars);
			}
		}
		vars = dictionary_ref(free_vars, k, SLY_NULL);
		if (list_member(kont->u.kproc.binding, vars)) {
			vars = list_remove(ss, vars, kont->u.kproc.binding);
			fprintf(file, "\tscm_value %s = self;\n",
					symbol_to_cid(kont->u.kproc.binding));
		}
		emit_c_unpack_self(vars, 0, file);
		_cps_emit_c(ss, graph, next->name, free_vars, constants, file);
	} break;
	case tt_cps_kreceive: {
		fprintf(file, closure_tmpl, "", symbol_to_cid(k), "");
		fprintf(file, "\tscm_chk_heap(&self);\n");
		struct arity_t arity = kont->u.kreceive.arity;
		fprintf(file, chk_args_tmpl, list_len(arity.req), booltoc(arity.rest));
		sly_assert(!booltoc(arity.rest), "unimplemented");
		CPS_Kont *next = cps_graph_ref(graph, kont->u.kreceive.k);
		emit_c_pop_list(list_reverse(ss, next->u.kargs.vars), file);
		sly_value vars = dictionary_ref(free_vars, k, SLY_NULL);
		fprintf(file, "\tscm_value k = closure_ref(self, 0);\n");
		emit_c_unpack_self(vars, 1, file);
		_cps_emit_c(ss, graph, next->name, free_vars, constants, file);
	} break;
	case tt_cps_ktail: {
		fprintf(file, tail_call_tmpl, "k");
	} break;
	default: sly_assert(0, "Unreachable");
	}
}

static void
emit_const_element(FILE *file, sly_value constants, sly_value elem)
{
	size_t idx;
	if (int_p(elem)) {
		fprintf(file, "\t\t{tt_int, .u.as_int=%ld},\n",
				get_int(elem));
	} else if (float_p(elem)) {
		fprintf(file, "\t\t{tt_float, .u.as_float=%g},\n",
				get_float(elem));
	} else if (true_p(elem)) {
		fprintf(file, "\t\t{tt_bool, .u.as_int=1},\n");
	} else if (false_p(elem)) {
		fprintf(file, "\t\t{tt_bool, .u.as_int=0},\n");
	} else if (null_p(elem)) {
		fprintf(file, "\t\t{tt_pair},\n");
	} else if (string_p(elem)) {
		idx = dictionary_ref(constants, elem, -1);
		sly_assert(idx != (size_t)-1, "Error constant not interned");
		fprintf(file, "\t\t{tt_box, .u.as_uint=%zu},\n", idx);
	} else if (symbol_p(elem)) {
		idx = dictionary_ref(constants, elem, -1);
		sly_assert(idx != (size_t)-1, "Error constant not interned");
		fprintf(file, "\t\t{tt_box, .u.as_uint=%zu},\n", idx);
	} else if (byte_vector_p(elem)) {
		idx = dictionary_ref(constants, elem, -1);
		sly_assert(idx != (size_t)-1, "Error constant not interned");
		fprintf(file, "\t\t{tt_box, .u.as_uint=%zu},\n", idx);
	} else if (vector_p(elem)) {
		idx = dictionary_ref(constants, elem, -1);
		sly_assert(idx != (size_t)-1, "Error constant not interned");
		fprintf(file, "\t\t{tt_box, .u.as_uint=%zu},\n", idx);
	} else if (pair_p(elem)) {
		idx = dictionary_ref(constants, elem, -1);
		sly_assert(idx != (size_t)-1, "Error constant not interned");
		fprintf(file, "\t\t{tt_box, .u.as_uint=%zu},\n", idx);
	} else {
		sly_assert(0, "unimplemented");
	}
}

void
cps_emit_c(Sly_State *ss, sly_value graph, sly_value start,
		   sly_value free_vars, FILE *file, int lib)
{
	char *buf;
	size_t buf_sz;
	FILE *buf_stream = open_memstream(&buf, &buf_sz);
	vector *vec = GET_PTR(free_vars);
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value entry = vec->elems[i];
		if (!slot_is_free(entry)) {
			sly_value k = car(entry);
			CPS_Kont *kont = cps_graph_ref(graph, k);
			if (kont->type != tt_cps_ktail) {
				fprintf(buf_stream, "scm_value %s(scm_value self);\n", symbol_to_cid(k));
			}
		}
	}
	fprintf(buf_stream, "\n");
	fprintf(buf_stream, closure_tmpl, "", symbol_to_cid(start), "entry");
	fprintf(buf_stream, "\tscm_chk_heap(&self);\n");
	fprintf(buf_stream, chk_args_tmpl, 2LU, 0);
	fprintf(buf_stream, pop_tmpl, "scm_value ", "k");
	fprintf(buf_stream, pop_tmpl, "scm_value ", "imports");
	sly_value vars = dictionary_ref(free_vars, start, SLY_NULL);
	if (list_len(vars) > 0) {
		while (!null_p(vars)) {
			sly_value id = car(vars);
			fprintf(buf_stream, "\tscm_value %s = scm_module_lookup(\"%s\", imports);\n",
					symbol_to_cid(id), symbol_to_cstr(id));
			vars = cdr(vars);
		}
	}
	sly_value constants = make_dictionary(ss);
	_cps_emit_c(ss, graph, start, free_vars, constants, buf_stream);
	fprintf(buf_stream, "}\n\n");
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value entry = vec->elems[i];
		if (!slot_is_free(entry) && !sly_equal(car(entry), start)) {
			sly_value k = car(entry);
			CPS_Kont *kont = cps_graph_ref(graph, k);
			if (kont->type != tt_cps_ktail) {
				_cps_emit_c(ss, graph, car(entry), free_vars, constants, buf_stream);
				fprintf(buf_stream, "}\n\n");
			}
		}
	}
	fprintf(buf_stream, init_top_level_tmpl, symbol_to_cid(start));
	if (!lib) {
		fprintf(buf_stream, main_tmpl);
	}
	fclose(buf_stream);
	fprintf(file, "#include \"scheme/scm_types.h\"\n");
	fprintf(file, "#include \"scheme/scm_runtime.h\"\n\n");
	fprintf(file, "static scm_value *interned;\n\n");
	vec = GET_PTR(constants);
	char *cbuf;
	size_t cbuf_sz;
	FILE *cbuf_stream = open_memstream(&cbuf, &cbuf_sz);
	fprintf(cbuf_stream, "static struct constant constants[] = {\n");
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value entry = vec->elems[i];
		if (!slot_is_free(entry)) {
			sly_value key = car(entry);
			size_t idx = cdr(entry);
			sly_value var = cps_gensym_temporary_name(ss);
			char *var_name = symbol_to_cid(var);
			if (string_p(key)) {
				fprintf(file, "static STATIC_String %s = "
						"{\n\t.len=%zu,\n\t.elems={",
						var_name, string_len(key));
				for (size_t i = 0; i < string_len(key); ++i) {
					fprintf(file, "%d,", get_byte(string_ref(ss, key, i)));
				}
				fprintf(file, "},\n};\n");
				fprintf(cbuf_stream, "\t[%zu] = {tt_string, .u.as_ptr=&%s},\n", idx, var_name);
			} else if (symbol_p(key)) {
				char *name = symbol_to_cstr(key);
				size_t len = strlen(name);
				fprintf(file, "static STATIC_Symbol %s = "
						"{\n\t.hash=%luLU,\n\t.len=%zu,\n\t.elems={",
						var_name, symbol_hash(key), len);
				for (size_t i = 0; i < len; ++i) {
					fprintf(file, "%d,", name[i]);
				}
				fprintf(file, "},\n};\n");
				fprintf(cbuf_stream, "\t[%zu] = {tt_symbol, .u.as_ptr=&%s},\n", idx, var_name);
			} else if (vector_p(key)) {
				vector *vec = GET_PTR(key);
				fprintf(file, "static STATIC_Vector %s = {\n"
						"\t.len=%zu,\n\t.elems={\n", var_name, vec->len);
				for (size_t i = 0; i < vec->len; ++i) {
					emit_const_element(file, constants, vec->elems[i]);
				}
				fprintf(file, "\t}\n");
				fprintf(file, "};\n");
				fprintf(cbuf_stream, "\t[%zu] = {tt_vector, .u.as_ptr=&%s},\n", idx, var_name);
			} else if (byte_vector_p(key)) {
				fprintf(file, "static STATIC_Bytevector %s = "
						"{\n\t.len=%zu,\n\t.elems={",
						var_name, byte_vector_len(key));
				for (size_t i = 0; i < byte_vector_len(key); ++i) {
					fprintf(file, "%d,", get_byte(byte_vector_ref(ss, key, i)));
				}
				fprintf(file, "},\n};\n");
				fprintf(cbuf_stream, "\t[%zu] = {tt_bytevector, .u.as_ptr=&%s},\n", idx, var_name);
			} else if (pair_p(key)) {
				sly_value lst = key;
				size_t len = 0;
				fprintf(file, "static STATIC_List %s = {\n"
						"\t.elems={\n", var_name);
				while (pair_p(lst)) {
					emit_const_element(file, constants, car(lst));
					len++;
					lst = cdr(lst);
				}
				emit_const_element(file, constants, lst);
				len++;
				fprintf(file, "\t},\n");
				fprintf(file, "\t.len=%zu\n};\n", len);
				fprintf(cbuf_stream, "\t[%zu] = {tt_pair, .u.as_ptr=&%s},\n", idx, var_name);
			} else {
				sly_assert(0, "unimplemented");
			}
			putc('\n', file);
		}
	}
	fprintf(cbuf_stream, "};\n");
	fclose(cbuf_stream);
	fwrite(cbuf, 1, cbuf_sz, file);
	putc('\n', file);
	fwrite(buf, 1, buf_sz, file);
}
