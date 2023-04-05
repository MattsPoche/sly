#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"
#include "gc.h"

#define SET_FRAME_UPVALUES()											\
	do {																\
		nframe->U = copy_vector(ss, clos->upvals);						\
		vector *upvec = GET_PTR(clos->upvals);							\
		size_t upidx = clos->arg_idx + proto->nargs;					\
		size_t len = vector_len(nframe->U);								\
		for (size_t i = upidx; i < len; ++i) {							\
			if (!ref_p(vector_ref(nframe->U, i))) {						\
				sly_value ref = (sly_value)((sly_value *)(&upvec->elems[i])); \
				ref = (ref & ~TAG_MASK) | st_ref;						\
				vector_set(nframe->U, i, ref);							\
			}															\
		}																\
	} while (0)

static inline sly_value
capture_value(stack_frame *frame, struct uplookup upinfo)
{
	while (frame->level != upinfo.level) {
		frame = frame->parent;
	}
	if (upinfo.isup) {
		vector *values = GET_PTR(frame->U);
		if (ref_p(values->elems[upinfo.reg])) {
			return values->elems[upinfo.reg];
		} else {
			sly_value v = (sly_value)(&values->elems[upinfo.reg]);
			return (v & ~TAG_MASK) | st_ref;
		}
	} else {
		vector *values = GET_PTR(frame->R);
		sly_value v = (sly_value)(&values->elems[upinfo.reg]);
		return (v & ~TAG_MASK) | st_ref;
	}
	sly_assert(0, "Error value not found");
	return SLY_NULL;
}

static inline int
is_ref_inframe(stack_frame *frame, sly_value *ref)
{
	vector *ups = GET_PTR(frame->U);
	sly_value *up_start = ups->elems;
	sly_value *up_end = &ups->elems[ups->len];
	sly_value *cl_start = NULL;
	sly_value *cl_end = NULL;
	if (!null_p(frame->clos)) {
		closure *clos = GET_PTR(frame->clos);
		ups = GET_PTR(clos->upvals);
		cl_start = ups->elems;
		cl_end = &ups->elems[ups->len];
	}
	vector *regs = GET_PTR(frame->R);
	sly_value *reg_start = regs->elems;
	sly_value *reg_end = &regs->elems[ups->len];
	return (ref >= up_start && ref < up_end)
		|| (ref >= cl_start && ref < cl_end)
		|| (ref >= reg_start && ref < reg_end);
}

static inline void
close_upvalues(stack_frame *frame)
{ /* If any closures are present on the stack,
   * check the upvalues. If the upvalue is a reference
   * that refers to a slot in the frame's upvalues or
   * registers, close the upvalue.
   */
	size_t rc = vector_len(frame->R);
	for (size_t i = 0; i < rc; ++i) {
		sly_value _clos = vector_ref(frame->R, i);
		if (closure_p(_clos)) {
			closure *clos = GET_PTR(_clos);
			prototype *proto = GET_PTR(clos->proto);
			size_t upidx = clos->arg_idx + proto->nargs;
			sly_value upvals = clos->upvals;
			size_t len = vector_len(upvals);
			for (size_t j = upidx; j < len; ++j) {
				sly_value uv = vector_ref(upvals, j);
				if (ref_p(uv)) {
					sly_value *ref = GET_PTR(uv);
					if (is_ref_inframe(frame, ref)) {
						vector_set(upvals, j, *ref); // close upvalue;
					}
				}
			}
		}
	}
}

sly_value
vm_run(Sly_State *ss)
{
	stack_frame *frame = ss->frame;
	INSTR ibits;
	struct instr *instr;
    for (;;) {
		ibits = next_instr();
		instr = (struct instr *)(&ibits);
		enum opcode i = GET_OP(instr);
		switch (i) {
		case OP_NOP: break;
		case OP_MOVE: {
			u8 a, b;
			a = GET_A(instr);
			b = GET_B(instr);
			set_reg(a, get_reg(b));
		} break;
		case OP_LOADI: {
			u8 a = GET_A(instr);
			i64 b = GET_sBx(instr);
			set_reg(a, make_int(ss, b));
		} break;
		case OP_LOADK: {
			u8 a = GET_A(instr);
			size_t b = GET_Bx(instr);
			set_reg(a, get_const(b));
		} break;
		case OP_LOADFALSE: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_FALSE);
		} break;
		case OP_LOADTRUE: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_TRUE);
		} break;
		case OP_LOADNULL: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_NULL);
		} break;
		case OP_LOADVOID: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_VOID);
		} break;
		case OP_GETUPVAL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			sly_value u = get_upval(b);
			if (ref_p(u)) {
				sly_value *ref = GET_PTR(u);
				set_reg(a, *ref);
			} else {
				set_reg(a, u);
			}
		} break;
		case OP_SETUPVAL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			sly_value u = get_upval(a);
			if (ref_p(u)) {
				sly_value *ref = GET_PTR(u);
				*ref = get_reg(b);
			} else {
				set_upval(a, get_reg(b));
			}
		} break;
		case OP_GETUPDICT: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value dict = get_upval(b);
			set_reg(a, dictionary_ref(dict, get_reg(c)));
		} break;
		case OP_SETUPDICT: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value dict = get_upval(a);
			dictionary_set(ss, dict, get_reg(b), get_reg(c));
		} break;
		case OP_CONS: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			set_reg(a, cons(ss, get_reg(b), get_reg(c)));
		} break;
		case OP_CAR: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			set_reg(a, car(get_reg(b)));
		} break;
		case OP_CDR: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			set_reg(a, cdr(get_reg(b)));
		} break;
		case OP_SETCAR: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			set_car(get_reg(a), get_reg(b));
		} break;
		case OP_SETCDR: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			set_cdr(get_reg(a), get_reg(b));
		} break;
		case OP_JMP: {
			u64 a = GET_Ax(instr);
			frame->pc = a;
		} break;
		case OP_FJMP: {
			u8 a = GET_A(instr);
			u64 b = GET_Bx(instr);
			if (get_reg(a) == SLY_FALSE) {
				frame->pc = b;
			}
		} break;
		case OP_CALL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			sly_value val = get_reg(a);
			if (cclosure_p(val)) {
				cclosure *clos = GET_PTR(val);
				size_t nargs = b - a - 1;
				sly_value args;
				if (clos->has_varg) {
					sly_assert(nargs >= clos->nargs,
							   "Error wrong number of arguments");
					size_t nvargs = nargs - clos->nargs;
					sly_value vargs = SLY_NULL;
					args = make_vector(ss, clos->nargs + 1, clos->nargs + 1);
					for (size_t i = b - 1; nvargs--; --i) {
						vargs = cons(ss, get_reg(i), vargs);
					}
					vector_set(args, clos->nargs, vargs);
					nargs = clos->nargs;
				} else {
					sly_assert(nargs == clos->nargs,
							   "Error wrong number of arguments");
					args = make_vector(ss, clos->nargs, clos->nargs);
				}
				for (size_t i = 0; i < nargs; ++i) {
					vector_set(args, i, get_reg(a + 1 + i));
				}
				sly_value r = clos->fn(ss, args);
				set_reg(a, r);
			} else if (closure_p(val)) {
				closure *clos = GET_PTR(val);
				prototype *proto = GET_PTR(clos->proto);
				stack_frame *nframe = make_stack(ss, proto->nregs);
				nframe->clos = val;
				nframe->level = frame->level + 1;
				size_t nargs = b - a - 1;
				SET_FRAME_UPVALUES();
				if (proto->has_varg) {
					sly_assert(nargs >= proto->nargs,
							   "Error wrong number of arguments");
					size_t nvargs = nargs - proto->nargs;
					sly_value vargs = SLY_NULL;
					for (size_t i = b - 1; nvargs--; --i) {
						vargs = cons(ss, get_reg(i), vargs);
					}
					vector_set(nframe->U, clos->arg_idx + proto->nargs, vargs);
				} else {
					sly_assert(nargs == proto->nargs,
							   "Error wrong number of arguments");
				}
				for (size_t i = 0; i < proto->nargs; ++i) {
					vector_set(nframe->U, clos->arg_idx + i, get_reg(a + 1 + i));
				}
				nframe->K = proto->K;
				nframe->code = proto->code;
				nframe->pc = proto->entry;
				nframe->ret_slot = a;
				nframe->parent = frame;
				frame = nframe;
			} else {
				sly_assert(0, "Type Error expected procedure");
			}
		} break;
		case OP_TAILCALL: {
			sly_assert(0, "(OP_TAILCALL) UNEMPLEMENTED");
		} break;
		case OP_RETURN: {
			u8 a = GET_A(instr);
			if (frame->parent == NULL) {
				return get_reg(a);
			}
			close_upvalues(frame);
			sly_value r = get_reg(a);
			size_t ret_slot = frame->ret_slot;
			frame = frame->parent;
			set_reg(ret_slot, r);
		} break;
		case OP_VECREF: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value vec = get_reg(b);
			set_reg(a, vector_ref(vec, get_int(get_reg(c))));
		} break;
		case OP_VECSET: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value vec = get_reg(a);
			vector_set(vec, get_int(get_reg(b)), get_reg(c));
		} break;
		case OP_DICTREF: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value dict = get_reg(b);
			set_reg(a, dictionary_ref(dict, get_reg(c)));
		} break;
		case OP_DICTSET: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value dict = get_reg(a);
			dictionary_set(ss, dict, get_reg(b), get_reg(c));
		} break;
		case OP_CLOSURE: {
			u8 a = GET_A(instr);
			size_t b = GET_Bx(instr);
			sly_value _proto = get_const(b);
			sly_value val = make_closure(ss, _proto);
			set_reg(a, val);
			closure *clos = GET_PTR(val);
			prototype *proto = GET_PTR(_proto);
			vector_set(clos->upvals, 0, vector_ref(frame->U, 0));
			size_t len = vector_len(proto->uplist);
			for (size_t i = 0; i < len; ++i) {
				sly_value upi = vector_ref(proto->uplist, i);
				struct uplookup upinfo = *((struct uplookup *)(&upi));
				sly_value uv = capture_value(frame, upinfo);
				vector_set(clos->upvals, i + 1 + proto->nargs, uv);
			}
		} break;
		case OP_DISPLAY: {
			u8 a = GET_A(instr);
			sly_value v = get_reg(a);
			sly_display(v, 0);
		} break;
		}
	}
	return SLY_VOID;
}
