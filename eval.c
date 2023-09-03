#include "sly_types.h"
#include "parser.h"
#include "sly_compile.h"
#define OPCODES_INCLUDE_INLINE 1
#include "opcodes.h"
#include "sly_vm.h"
#include "eval.h"

stack_frame *
make_eval_stack(Sly_State *ss, sly_value regs)
{
	stack_frame *frame = gc_alloc(ss, sizeof(*frame));
	frame->h.type = tt_stack_frame;
	frame->cont = SLY_NULL;
	frame->R = regs;
	frame->code = make_vector(ss, 0, 4);
	frame->pc = 0;
	frame->level = 0;
	frame->clos = SLY_NULL;
	return frame;
}

sly_value
call_closure(Sly_State *ss, sly_value call_list)
{
	stack_frame *nframe = make_eval_stack(ss, call_list);
	size_t len = vector_len(nframe->R);
	vector_append(ss, nframe->code, iAB(OP_CALL, 0, len, -1));
	//vector_append(ss, nframe->code, iA(OP_RETURN, 0, -1));
	stack_frame *tmp = ss->frame;
	ss->frame = nframe;
	ss->frame->clos = vector_ref(call_list, 0);
	closure *clos = GET_PTR(ss->frame->clos);
	ss->frame->U = clos->upvals;
	sly_value val = vm_run(ss, 0);
	ss->frame = tmp;
	return val;
}

sly_value
eval_closure(Sly_State *ss, sly_value _clos, sly_value args, int gc_flag)
{
	stack_frame *tmp = ss->frame;
	ss->frame = gc_alloc(ss, sizeof(stack_frame));
	ss->frame->h.type = tt_stack_frame;
	ss->frame->cont = SLY_NULL;
	closure *clos = GET_PTR(_clos);
	prototype *proto = GET_PTR(clos->proto);
	ss->frame->K = proto->K;
	ss->frame->U = clos->upvals;
	size_t len = proto->nregs + proto->has_varg;
	size_t nargs = proto->nargs + proto->has_varg;
	ss->frame->R = make_vector(ss, len, len);
	if (nargs) {
		for (size_t i = 0; i < nargs; ++i) {
			vector_set(ss->frame->R, i, vector_ref(args, i));
		}
	}
	ss->frame->code = proto->code;
	ss->frame->pc = proto->entry;
	ss->frame->level = 0;
	ss->frame->clos = _clos;
	ss->frame->cont = SLY_NULL;
	sly_value rv = vm_run(ss, gc_flag);
	ss->frame = tmp;
	return rv;
}
