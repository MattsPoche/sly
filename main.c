#include <stdio.h>
#include <ctype.h>
#ifndef NO_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#include "sly_types.h"
#include "gc.h"
#include "sly_vm.h"
#include "sly_alloc.h"
#include "parser.h"
#include "sly_compile.h"
#include "opcodes.h"
#include "eval.h"

static int allocations = 0;
static int net_allocations = 0;
static size_t bytes_allocated = 0;

static int debug_info = 0;

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

/* TODO: refactor relevent code to use
 * exceptions over assert.
 * TODO: create a sly_exception type?
 */

void
sly_init_state(Sly_State *ss)
{
	allocations = 0;
	net_allocations = 0;
	bytes_allocated = 0;
	memset(ss, 0, sizeof(*ss));
}

void
sly_free_state(Sly_State *ss)
{
	gc_free_all(ss);
	if (ss->cc) {
		FREE(ss->cc);
		ss->cc = NULL;
	}
	if (ss->source_code) {
		FREE(ss->source_code);
		ss->source_code = NULL;
	}
}

sly_value
sly_read_file(Sly_State *ss, char *file_name)
{
	ss->file_path = file_name;
	ss->interned = make_dictionary(ss);
	return parse_file(ss, file_name, &ss->source_code);
}

sly_value
sly_load_file(Sly_State *ss, char *file_name)
{
	sly_value ast = sly_read_file(ss, file_name);
	if (debug_info) {
		printf("Compiling file %s ...\n", file_name);
	}
	if (sly_compile(ss, ast) != 0) {
		printf("Unable to run file %s\n", file_name);
		return SLY_VOID;
	}
	ss->proto = ss->cc->cscope->proto;
	prototype *proto = GET_PTR(ss->cc->cscope->proto);
	stack_frame *frame = make_stack(ss, proto->nregs);
	frame->U = make_vector(ss, 12, 12);
	for (size_t i = 0; i < 12; ++i) {
		vector_set(frame->U, i, SLY_VOID);
	}
	vector_set(frame->U, 0, make_closed_upvalue(ss, ss->cc->globals));
	frame->K = proto->K;
	frame->clos = SLY_NULL;
	frame->code = proto->code;
	frame->pc = proto->entry;
	frame->level = 0;
	ss->frame = frame;
	gc_collect(ss);
	if (debug_info) {
		dis_all(ss->frame, 1);
	}
	printf("Running file: %s\n", file_name);
	printf("Output:\n");
	return vm_run(ss, 1);
}

char *
rl_gets(char *prompt)
{
#ifdef NO_READLINE
	static char line[255] = {0};
	printf("%s", prompt);
	fgets(line, sizeof(line), stdin);
	size_t len = strlen(line);
	if (line[len-1] == '\n') {
		line[len-1] = '\0';
	}
	return line;
#else
	static char *line = NULL;
	if (line) {
		free(line);
		line = NULL;
	}
	line = readline(prompt);
	if (line && line[0]) {
		add_history(line);
	}
	return line;
#endif
}

void
sly_repl(Sly_State *ss)
{
	ss->file_path = "repl-env";
	ss->interned = make_dictionary(ss);
	sly_compile(ss, parse(ss, "(display \"Hello, welcome to Sly\\n\")"));
	ss->proto = ss->cc->cscope->proto;
	prototype *proto = GET_PTR(ss->cc->cscope->proto);
	stack_frame *frame = make_stack(ss, proto->nregs);
	frame->U = make_vector(ss, 12, 12);
	for (size_t i = 0; i < 12; ++i) {
		vector_set(frame->U, i, SLY_VOID);
	}
	vector_set(frame->U, 0, ss->cc->globals);
	frame->K = proto->K;
	frame->clos = SLY_NULL;
	frame->code = proto->code;
	frame->pc = proto->entry;
	frame->level = 0;
	ss->frame = frame;
	ss->eval_frame = make_eval_stack(ss);
	gc_collect(ss);
	vm_run(ss, 1);
	for (;;) {
		char *line = rl_gets("> ");
		if (line == NULL) continue;
		for (; isspace((int)(*line)); line++);
		if (*line == '\0') continue;
		if (*line == ',') {
			if (strcmp(line, ",quit") == 0
				|| strcmp(line, ",q") == 0) {
				break;
			} else if (strcmp(line, ",dis") == 0) {
				dis_all(frame, 1);
				continue;
			} else {
				printf("Unknown command\n");
				continue;
			}
		}
		sly_value v = eval_expr(ss, parse(ss, line));
		if (!void_p(v)) {
			sly_display(v, 1);
			printf("\n");
		}
		GARBAGE_COLLECT(ss);
	}
}

char *
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
	Sly_State ss = {0};
	char *arg = next_arg(&argc, &argv);
	if (argc) {
		while (argc) {
			arg = next_arg(&argc, &argv);
			if (strcmp(arg, "-I") == 0) {
				debug_info = 1;
				continue;
			}
			sly_init_state(&ss);
			sly_load_file(&ss, arg);
			sly_free_state(&ss);
			if (debug_info) {
				printf("** Allocations: %d **\n", allocations);
				printf("** Net allocations: %d **\n", net_allocations);
				printf("** Total bytes allocated: %zu **\n", bytes_allocated);
				printf("** GC Total Collections: %d **\n\n", ss.gc.collections);
			} else {
				printf("\n");
			}
		}
	} else {
		sly_init_state(&ss);
		sly_repl(&ss);
	}
	return 0;
}
