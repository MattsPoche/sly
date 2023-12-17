#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <gc.h>
#include <dlfcn.h>
#include "sly_types.h"
#include "cps.h"
#include "cbackend.h"

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
				{
					// gcc -fPIC -shared -ggdb -o test.so test.sly.c
					FILE *file = fopen("test.sly.c", "w");
					cps_emit_c(&ss, graph, entry, free_var_lookup, file, 1);
					fclose(file);
					system("gcc -fPIC -shared -ggdb -o test.so test.sly.c");
					void *handle = dlopen("./test.so", RTLD_NOW);
					assert(handle != NULL);
					typedef void * __attribute__((__may_alias__)) pvoid_may_alias;
					u64 (*init_top_level)(void);
					*(pvoid_may_alias *)(&init_top_level) = dlsym(handle, "init_top_level");
					void (*trampoline)(u64);
					*(pvoid_may_alias *)(&trampoline) = dlsym(handle, "trampoline");
					printf("BEGIN-FILE:\n");
					trampoline(init_top_level());
					dlclose(handle);
				}
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
