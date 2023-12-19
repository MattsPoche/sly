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
#if 1
				// ./cbuild.sh --clean && make -k && ./sly --expand test/test.sly
				// gcc -fPIC -shared -ggdb -o test.so test.sly.c
				FILE *file = fopen("test.sly.c", "w");
				cps_emit_c(&ss, graph, entry, free_var_lookup, file, 1);
				fclose(file);
				int r = system("gcc -fPIC -c -ggdb -o scheme/scm_runtime.o scheme/scm_runtime.c");
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
				FILE *file = fopen("test.sly.c", "w");
				cps_emit_c(&ss, graph, entry, free_var_lookup, file, 0);
				fclose(file);
				int r = system("gcc -c -ggdb -o scheme/scm_runtime.o scheme/scm_runtime.c");
				sly_assert(r == 0, "compile error");
				r = system("gcc -o test.e test.sly.c scheme/scm_runtime.o");
				sly_assert(r == 0, "compile error");
#endif
			}
		}
	} else {
		printf("No source file provided.\nExiting ...\n");
	}
	return 0;
}
