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
	next_arg(&argc, &argv);
	if (argc) {
		while (argc) {
			Sly_State ss = {0};
			sly_value ast = sly_expand_only(&ss, next_arg(&argc, &argv));
			return compile_form(&ss, ast);
#if 0
			{
#if 0
				// ./cbuild.sh --clean && make -k && ./sly --expand test/test.sly
				// gcc -fPIC -shared -ggdb -o test.so test.sly.c
				FILE *file = fopen("test.sly.c", "w");
				if (file == NULL) {
					fprintf(stderr, "Error opening file\n");
					return 1;
				}
				cps_emit_c(&ss, graph, entry, free_var_lookup, file, 1);
				fclose(file);
				int r = system("./scheme/build_runtime.sh -s");
				sly_assert(r == 0, "compile error");
				r = system("gcc -fPIC -shared -ggdb -o test.so test.sly.c scheme/scm_runtime.o");
				sly_assert(r == 0, "compile error");
				void *handle = dlopen("./test.so", RTLD_NOW);
				if (handle == NULL) {
					printf("ERROR:\n%s\n", dlerror());
					assert(handle != NULL);
				}
				typedef void * __attribute__((__may_alias__)) pvoid_may_alias;
				u64 (*load_dynamic)(void);
				*(pvoid_may_alias *)(&load_dynamic) = dlsym(handle, "load_dynamic");
				int (*trampoline)(u64);
				*(pvoid_may_alias *)(&trampoline) = dlsym(handle, "trampoline");
				void (*scm_heap_init)(void);
				*(pvoid_may_alias *)(&scm_heap_init) = dlsym(handle, "scm_heap_init");
				printf("BEGIN-FILE:\n");
				scm_heap_init();
				trampoline(load_dynamic());
				dlclose(handle);
#else
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
				FILE *file = fopen("test.sly.c", "w");
				cps_emit_c(&ss, graph, entry, free_var_lookup, file, 0);
				fclose(file);
				int r = system("./scheme/build_runtime.sh");
				sly_assert(r == 0, "compile error");
				r = system("gcc -o test.e test.sly.c scheme/scm_runtime.o");
				sly_assert(r == 0, "compile error");
#endif
			}
#endif
		}
	} else {
		printf("No source file provided.\nExiting ...\n");
	}
	return 0;
}
