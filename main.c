#include <stdio.h>
#include <string.h>
#include <gc.h>
#include "sly_types.h"
#include "cps.h"

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

UNUSED_ATTR static AMI_Inst
ami_make_inst(u8 op, u8 m0, u8 m1, u8 m2, u8 m3,
			  u8 v0, u8 v1, u8 v2, u8 v3)
{
	u8 modes = 0;
	modes = ARG_SET_MODE(modes, 0, m0);
	modes = ARG_SET_MODE(modes, 1, m1);
	modes = ARG_SET_MODE(modes, 2, m2);
	modes = ARG_SET_MODE(modes, 3, m3);
	AMI_Inst inst = {0};
	inst.u.inst.args[0] = v0;
	inst.u.inst.args[1] = v1;
	inst.u.inst.args[2] = v2;
	inst.u.inst.args[3] = v3;
	inst.u.inst.modes = modes;
	inst.u.inst.op = op;
	return inst;
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
				printf("69 := 0x%lx\n", make_int(&ss, 69) & 0xffffffff);
				printf("69.0 := 0x%lx\n", make_float(&ss, 69.0f) & 0xffffffff);
				sly_value graph = make_dictionary(&ss);
				sly_value name = make_symbol(&ss, "$tkexit", 7);
				CPS_Kont *kont = cps_make_ktail(&ss, 0);
				kont->name = name;
				cps_graph_set(&ss, graph, name, kont);
				sly_displayln(strip_syntax(ast));
				cps_init_primops(&ss);
				sly_value entry = cps_translate(&ss, name, graph, ast);
				cps_display(&ss, graph, entry);
				printf("========================================================\n");
				graph = cps_opt_contraction_phase(&ss, graph, entry, 1);
				cps_display(&ss, graph, entry);
				printf("\n");
				sly_value var_info = cps_collect_var_info(&ss, graph,
														  make_dictionary(&ss),
														  make_dictionary(&ss),
														  make_dictionary(&ss), NULL, entry);
				sly_value free_var_lookup = cps_collect_free_variables(&ss, graph, var_info, entry);
				printf("free vars:\n");
				vector *vec = GET_PTR(free_var_lookup);
				for (size_t i = 0; i < vec->cap; ++i) {
					sly_value entry = vec->elems[i];
					if (!slot_is_free(entry)) {
						sly_display(car(entry), 1);
						printf(" = ");
						sly_displayln(cdr(entry));
					}
				}
				printf("\n");
				sly_value free_vars = dictionary_ref(free_var_lookup, entry, SLY_VOID);
				struct kclosure_t clos = {
					.clos_def = SLY_NULL,
					.clos_shares = SLY_NULL,
					.cc_name = SLY_FALSE,
					.kr_name = SLY_FALSE,
					.offset = 0,
					.kr_size = 0,
				};
				entry = cps_opt_closure_convert(&ss, graph, &clos,
												free_var_lookup,
												free_vars, entry);
				printf("CLOSURE-CONVERSION:\n");
				cps_display(&ss, graph, entry);
				cps_display_free_vars_foreach_k(&ss, graph, entry);
				sly_displayln(ami_convert(&ss));
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
