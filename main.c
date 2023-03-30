#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"

typedef struct sly_state {
	sly_value interned;  // <alist> of interned symbols
	stack_frame *begin;  // top level code
} Sly_State;

sly_value
vm_run(stack_frame *frame)
{
	int jmp_limit = 5;
	int jmps = 0;
	INSTR instr;
    for (;;) {
		instr = next_instr();
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
			set_reg(a, make_int(b));
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
			set_reg(a, get_upval(b));
		} break;
		case OP_SETUPVAL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			set_upval(a, get_reg(b));
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
			dictionary_set(dict, get_reg(b), get_reg(c));
		} break;
		case OP_CONS: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			set_reg(a, cons(get_reg(b), get_reg(c)));
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
#if 0
		case OP_ADD: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			set_reg(a, sly_add(get_reg(b), get_reg(c)));
		} break;
		case OP_SUB: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			set_reg(a, sly_sub(get_reg(b), get_reg(c)));
		} break;
		case OP_MUL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			set_reg(a, sly_mul(get_reg(b), get_reg(c)));
		} break;
		case OP_DIV: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			set_reg(a, sly_div(get_reg(b), get_reg(c)));
		} break;
		case OP_MOD: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			set_reg(a, sly_mod(get_reg(b), get_reg(c)));
		} break;
#endif
		case OP_JMP: {
			if (jmps == jmp_limit) return SLY_NULL;
			jmps++;
			u64 a = GET_Ax(instr);
			frame->pc = a;
		} break;
		case OP_FJMP: {
			if (jmps == jmp_limit) return SLY_NULL;
			jmps++;
			u8 a = GET_A(instr);
			u64 b = GET_Bx(instr);
			if (get_reg(a) == SLY_FALSE) {
				frame->pc = b;
			}
		} break;
#if 0
		case OP_JGZ: {
			u8 a = GET_A(instr);
			u64 b = GET_Bx(instr);
			if (get_int(get_reg(a)) > 0) {
				frame->pc = b;
			}
		} break;
		case OP_JLZ: {
			u8 a = GET_A(instr);
			u64 b = GET_Bx(instr);
			if (get_int(get_reg(a)) < 0) {
				frame->pc = b;
			}
		} break;
		case OP_JEZ: {
			u8 a = GET_A(instr);
			u64 b = GET_Bx(instr);
			if (get_int(get_reg(a)) == 0) {
				frame->pc = b;
			}
		} break;
		case OP_JNZ: {
			u8 a = GET_A(instr);
			u64 b = GET_Bx(instr);
			if (get_int(get_reg(a)) != 0) {
				frame->pc = b;
			}
		} break;
#endif
		case OP_CALL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			sly_value val = get_reg(a);
			if (cclosure_p(val)) {
				cclosure *clos = GET_PTR(val);
				size_t nargs = b - a - 1;
				sly_value args;
				if (clos->has_varg) {
					sly_assert(nargs >= clos->nargs, "Error wrong number of arguments");
					size_t nvargs = nargs - clos->nargs;
					sly_value vargs = SLY_NULL;
					args = make_vector(clos->nargs + 1, clos->nargs + 1);
					for (size_t i = b - 1; nvargs--; --i) {
						vargs = cons(get_reg(i), vargs);
					}
					vector_set(args, clos->nargs, vargs);
					nargs = clos->nargs;
				} else {
					sly_assert(nargs == clos->nargs, "Error wrong number of arguments");
					args = make_vector(clos->nargs, clos->nargs);
				}
				for (size_t i = 0; i < nargs; ++i) {
					vector_set(args, i, get_reg(a + 1 + i));
				}
				sly_value r = clos->fn(args);
				set_reg(a, r);
			} else if (closure_p(val)) {
				closure *clos = GET_PTR(val);
				prototype *proto = GET_PTR(clos->proto);
				size_t nargs = b - a - 1;
				sly_assert(nargs == proto->nargs, "Error wrong number of arguments");
				stack_frame *nframe = make_stack(proto->nregs);
				nframe->U = copy_vector(clos->upvals);
				for (size_t i = 0; i < nargs; ++i) {
					vector_set(nframe->U, i + clos->arg_idx, get_reg(a + 1 + i));
				}
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
			stack_frame *pframe = frame;
			frame = frame->parent;
			free(pframe);
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
			dictionary_set(dict, get_reg(b), get_reg(c));
		} break;
		case OP_CLOSURE: {
			u8 a = GET_A(instr);
			size_t b = GET_Bx(instr);
			sly_value _proto = get_const(b);
			sly_value val = make_closure(_proto);
			closure *clos = GET_PTR(val);
			prototype *proto = GET_PTR(_proto);
			if (!null_p(proto->uplist)) {
				size_t len = byte_vector_len(proto->uplist);
				/* capture enclosing scope */
				for (size_t i = 0; i < len; ++i) {
					vector_set(clos->upvals, i, get_reg(byte_vector_ref(proto->uplist, i)));
				}
			}
			set_reg(a, val);
		} break;
		case OP_DISPLAY: {
			u8 a = GET_A(instr);
			sly_value v = get_reg(a);
			if (int_p(v)) {
				i64 n = get_int(v);
				printf("%ld\n", n);
			} else if (float_p(v)) {
				f64 n = get_float(v);
				printf("%g\n", n);
			} else {
				sly_assert(0, "UNEMPLEMENTED");
			}
		} break;
		}
	}
	return 0;
}

void
dis(INSTR instr)
{
	enum opcode op = GET_OP(instr);
	switch (op) {
	case OP_NOP: break;
	case OP_MOVE: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(MOVE %d %d)\n", a, b);
	} break;
	case OP_LOADI: {
		u8 a = GET_A(instr);
		i64 b = GET_Bx(instr);
		printf("(LOADI %d %ld)\n", a, b);
	} break;
	case OP_LOADK: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(LOADK %d %lu)\n", a, b);
	} break;
	case OP_LOAD_FALSE: {
		u8 a = GET_A(instr);
		printf("(OP_LOAD_FALSE %d)\n", a);
	} break;
	case OP_LOAD_TRUE: {
		u8 a = GET_A(instr);
		printf("(OP_LOAD_TRUE %d)\n", a);
	} break;
	case OP_LOAD_NULL: {
		u8 a = GET_A(instr);
		printf("(OP_LOAD_NULL %d)\n", a);
	} break;
	case OP_LOAD_VOID: {
		u8 a = GET_A(instr);
		printf("(OP_LOAD_VOID %d)\n", a);
	} break;
	case OP_GETUPVAL: {
	} break;
	case OP_SETUPVAL: {
	} break;
	case OP_GETUPDICT: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(GETUPDICT %d %d %d)\n", a, b, c);
	} break;
	case OP_SETUPDICT: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(SETUPDICT %d %d %d)\n", a, b, c);
	} break;
	case OP_CONS: {
	} break;
	case OP_CAR: {
	} break;
	case OP_CDR: {
	} break;
	case OP_SETCAR: {
	} break;
	case OP_SETCDR: {
	} break;
#if 0
	case OP_ADD: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(ADD %d %d %d)\n", a, b, c);
	} break;
	case OP_SUB: {
	} break;
	case OP_MUL: {
	} break;
	case OP_DIV: {
	} break;
	case OP_MOD: {
	} break;
#endif
	case OP_JMP: {
		u64 a = GET_Ax(instr);
		printf("(JMP %lu)\n", a);
	} break;
	case OP_FJMP: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(FJMP %d %lu)\n", a, b);
	} break;
#if 0
	case OP_JGZ: {
	} break;
	case OP_JLZ: {
	} break;
	case OP_JEZ: {
	} break;
	case OP_JNZ: {
	} break;
#endif
	case OP_CALL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(CALL %d %d)\n", a, b);
	} break;
	case OP_TAILCALL: {
	} break;
	case OP_RETURN: {
		u8 a = GET_A(instr);
		printf("(RETURN %d)\n", a);
	} break;
	case OP_DICTREF: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(DICTREF %d %d %d)\n", a, b, c);
	} break;
	case OP_DICTSET: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(DICTSET %d %d %d)\n", a, b, c);
	} break;
	case OP_VECREF: {
	} break;
	case OP_VECSET: {
	} break;
	case OP_CLOSURE: {
	} break;
	case OP_DISPLAY: {
		u8 a = GET_A(instr);
		printf("(DISPLAY %d)\n", a);
	} break;
	}
}

sly_value
run_file(char *file_name)
{
	printf("Running file %s\n", file_name);
	struct compile cc = sly_compile(file_name);
	prototype *proto = GET_PTR(cc.cscope->proto);
	stack_frame *frame = make_stack(proto->nregs);
	size_t len = vector_len(proto->code);
	for (size_t i = 0; i < len; ++i) {
		dis(vector_ref(proto->code, i));
	}
	printf("============================================\n");
	frame->U = make_vector(12, 12);
	vector_set(frame->U, 0, cc.globals);
	frame->K = proto->K;
	frame->code = proto->code;
	frame->pc = proto->entry;
	return vm_run(frame);
}

int
main(int argc, char *argv[])
{
	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			run_file(argv[i]);
		}
	} else {
		run_file("test.scm");
	}
	return 0;
}
