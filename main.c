#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"
#include "gc.h"

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
			i64 b = (i64)GET_Bx(instr);
			//printf("b :: %ld\n", b);
			set_reg(a, make_int(ss, b));
		} break;
		case OP_LOADK: {
			u8 a = GET_A(instr);
			size_t b = GET_Bx(instr);
			set_reg(a, get_const(b));
		} break;
		case OP_LOAD_FALSE: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_FALSE);
		} break;
		case OP_LOAD_TRUE: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_TRUE);
		} break;
		case OP_LOAD_NULL: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_NULL);
		} break;
		case OP_LOAD_VOID: {
			u8 a = GET_A(instr);
			set_reg(a, SLY_VOID);
		} break;
		case OP_GETUPVAL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			sly_value u = get_upval(b);
			if (ref_p(u)) {
				set_reg(a, (sly_value)GET_PTR(u));
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
				size_t nargs = b - a - 1;
				nframe->U = copy_vector(ss, clos->upvals);
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
			sly_assert(0, "UNEMPLEMENTED");
		} break;
		case OP_RETURN: {
			u8 a = GET_A(instr);
			if (frame->parent == NULL) {
				return get_reg(a);
			}
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
			closure *clos = GET_PTR(val);
			vector_set(clos->upvals, 0, vector_ref(frame->U, 0));
			set_reg(a, val);
		} break;
		case OP_DISPLAY: {
			u8 a = GET_A(instr);
			sly_value v = get_reg(a);
			sly_display(v, 0);
		} break;
		}
	}
	return 0;
}

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
	frame->code = proto->code;
	frame->pc = proto->entry;
	ss->frame = frame;
	printf("Running file %s\n", file_name);
	dis_all(ss->frame, 1);
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
