#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"
#include "gc.h"
#include "sly_vm.h"

void
sly_init_state(Sly_State *ss)
{
	gc_init(&ss->gc);
}

void
sly_free_state(Sly_State *ss)
{
	free(ss->gc.w.cells);
	free(ss->gc.f.cells);
}

void
sly_load_file(Sly_State *ss, char *file_name)
{
	sly_compile(ss, file_name);
	prototype *proto = GET_PTR(ss->cc->cscope->proto);
	stack_frame *frame = make_stack(ss, proto->nregs);
	frame->U = make_vector(ss, 12, 12);
	vector_set(frame->U, 0, ss->cc->globals);
	frame->K = proto->K;
	frame->clos = SLY_NULL;
	frame->code = proto->code;
	frame->pc = proto->entry;
	frame->level = 0;
	ss->frame = frame;
	printf("Running file %s\n", file_name);
	dis_all(ss->frame, 1);
	printf("Output:\n");
	vm_run(ss);
	ss->code = make_vector(ss, 0, 1);
	ss->stack = make_vector(ss, 0, 16);
	ss->frame = frame;
}

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
	sly_assert(nargs + 1 <= end, "Error not enough arguments");
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

int
main(int argc, char *argv[])
{
	Sly_State ss;
	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			sly_init_state(&ss);
			sly_load_file(&ss, argv[i]);
			sly_free_state(&ss);
		}
	} else {
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
	return 0;
}
