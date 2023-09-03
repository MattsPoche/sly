#include <stdio.h>
#include <ctype.h>
#include "sly_types.h"
#include "gc.h"
#include "sly_alloc.h"
#include "parser.h"
#include "sly_compile.h"
#include "opcodes.h"
#include "eval.h"
#include "sly_vm.h"

int allocations = 0;
int net_allocations = 0;
size_t bytes_allocated = 0;

void *
sly_alloc(size_t size)
{
	void *ptr = calloc(size, 1);
	bytes_allocated += size;
	allocations++;
	net_allocations++;
	return ptr;
}

void
sly_free(void *ptr)
{
	free(ptr);
	net_allocations--;
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
			} else {
				allocations = 0;
				net_allocations = 0;
				bytes_allocated = 0;
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
