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
				entry = cps_opt_closure_convert(&ss, graph, free_var_lookup,
												free_vars, entry);
				printf("CLOSURE-CONVERSION:\n");
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
