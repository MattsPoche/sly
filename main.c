#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"
#include "gc.h"
#include "sly_vm.h"
#include "sly_alloc.h"

static int allocations = 0;
static int net_allocations = 0;
static size_t bytes_allocated = 0;

/*
? 0x560e98b36480
? 0x560e98b368b0
? 0x560e98b368f0
? 0x560e98b36930
? 0x560e98b36980
> 0x560e98b36e60
*/
/*
free :: 0x55b9da3c9e90
count :: 186608
double-free?
addr  :: 0x55b9da3c9e60
free :: 0x55b9da3c9e60
free :: 0x55b9da3c9e40
free :: 0x55b9da3c9e00
free :: 0x55b9da3c9de0
free :: 0x55b9da3c9da0
free :: 0x55b9da3cc1e0

free :: 0x55b9da3e5130
free :: 0x55b9da3f27f0
free :: 0x55b9da3f3530
*/
void *
sly_alloc(size_t size)
{
	void *ptr = malloc(size);
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
	gc_init(&ss->gc);
}

void
sly_free_state(Sly_State *ss)
{
	gc_free_all(ss);
	if (ss->cc->cscope)  FREE(ss->cc->cscope);
	if (ss->cc)          FREE(ss->cc);
	if (ss->source_code) FREE(ss->source_code);
}

void
sly_load_file(Sly_State *ss, char *file_name)
{
	ss->gc.nocollect = 1;
	if (sly_compile(ss, file_name) != 0) {
		printf("Unable to run file %s\n", file_name);
		return;
	}
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
	dis_all(ss->frame, 1);
	//ss->gc.nocollect = 0;
	gc_collect(ss);
	printf("Running file %s\n", file_name);
	dis_all(ss->frame, 1);
	printf("Output:\n");
	vm_run(ss);
	ss->code = make_vector(ss, 0, 1);
	ss->stack = make_vector(ss, 0, 16);
}

#if 0
void
sly_push_global(Sly_State *ss, char *name)
{
	stack_frame *frame = ss->frame;
	sly_value globals = vector_ref(frame->U, 0);
	sly_value key = make_symbol(ss, name, strlen(name));
	vector_append(ss, ss->stack, dictionary_ref(globals, key));
}

void
sly_push_int(Sly_State *ss, i64 i)
{
	vector_append(ss, ss->stack, make_int(ss, i));
}

void
sly_push_symbol(Sly_State *ss, char *name)
{
	vector_append(ss, ss->stack, make_symbol(ss, name, strlen(name)));
}

void
sly_push_cstr(Sly_State *ss, char *str)
{
	vector_append(ss, ss->stack, make_string(ss, str, strlen(str)));
}

sly_value
sly_call(Sly_State *ss, size_t nargs)
{
	size_t end = vector_len(ss->stack);
	if (nargs + 1 > end) {
		sly_raise_exception(ss, EXC_ARGS, "Error not enough arguments");
	}
	size_t begin = end - (nargs + 1);
	ss->code = make_vector(ss, 0, 2);
	vector_append(ss, ss->code, iAB(OP_CALL, begin, end, -1));
	vector_append(ss, ss->code, iA(OP_RETURN, begin, -1));
	sly_value R = ss->frame->R;
	sly_value code = ss->frame->code;
	size_t pc = ss->frame->pc;
	ss->frame->R = ss->stack;
	ss->frame->code = ss->code;
	ss->frame->pc = 0;
	sly_value r = vm_run(ss);
	ss->frame->R = R;
	ss->frame->code = code;
	ss->frame->pc = pc;
	return r;
}
#endif

int
main(int argc, char *argv[])
{
	Sly_State ss = {0};
	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			sly_init_state(&ss);
			sly_load_file(&ss, argv[i]);
			printf("** GC Object count: %zu **\n", ss.gc.obj_count);
			printf("** GC bytes: %zu **\n" , ss.gc.bytes);
			sly_free_state(&ss);
			printf("** Allocations: %d **\n", allocations);
			printf("** Net allocations: %d **\n", net_allocations);
			printf("** Total bytes allocated: %zu **\n\n", bytes_allocated);
		}
	}
#if 0
	else {
		sly_init_state(&ss);
		sly_load_file(&ss, "test_rec.scm");
		dis_all(ss.frame, 1);
		sly_push_global(&ss, "fib");
		sly_push_int(&ss, 10);
		sly_display(sly_call(&ss, 1), 1);
		printf("\n");
		sly_push_global(&ss, "fac");
		sly_push_int(&ss, 10);
		sly_display(sly_call(&ss, 1), 1);
		printf("\n");
		sly_init_state(&ss);
	}
#endif
	return 0;
}
