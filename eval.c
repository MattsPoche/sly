#include "sly_types.h"
#include "sly_vm.h"
#include "parser.h"
#include "sly_compile.h"
#define OPCODES_INCLUDE_INLINE 1
#include "opcodes.h"
#include "eval.h"

sly_value
call_closure(Sly_State *ss, sly_value call_list)
{
	stack_frame *nframe = make_eval_stack(ss, call_list);
	size_t len = vector_len(nframe->R);
	vector_append(ss, nframe->code, iAB(OP_CALL, 0, len, -1));
	vector_append(ss, nframe->code, iA(OP_RETURN, 0, -1));
	stack_frame *tmp = ss->frame;
	ss->frame = nframe;
	sly_value val = vm_run(ss, 0);
	ss->frame = tmp;
	return val;
}
