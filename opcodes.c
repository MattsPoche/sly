#include "sly_types.h"
#include "opcodes.h"

stack_frame *
make_stack(Sly_State *ss, size_t nregs)
{
	sly_assert(nregs <= REG_MAX, "Stack too big");
	stack_frame *frame = sly_alloc(ss, sizeof(*frame));
	frame->parent = NULL;
	frame->R = make_vector(ss, nregs, nregs);
	return frame;
}

void
dis(INSTR ins)
{
	struct instr *instr;
	instr = (struct instr *)(&ins);
	enum opcode op = GET_OP(instr);
	switch (op) {
	case OP_NOP: break;
	case OP_MOVE: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(MOVE %d %d) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_LOADI: {
		u8 a = GET_A(instr);
		i64 b = GET_Bx(instr);
		printf("(LOADI %d %ld) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_LOADK: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(LOADK %d %lu) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_LOAD_FALSE: {
		u8 a = GET_A(instr);
		printf("(LOAD_FALSE %d) ;; line %d\n", a, instr->ln);
	} break;
	case OP_LOAD_TRUE: {
		u8 a = GET_A(instr);
		printf("(LOAD_TRUE %d) ;; line %d\n", a, instr->ln);
	} break;
	case OP_LOAD_NULL: {
		u8 a = GET_A(instr);
		printf("(LOAD_NULL %d) ;; line %d\n", a, instr->ln);
	} break;
	case OP_LOAD_VOID: {
		u8 a = GET_A(instr);
		printf("(LOAD_VOID %d) ;; line %d\n", a, instr->ln);
	} break;
	case OP_GETUPVAL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(GETUPVAL %d %d) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_SETUPVAL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(SETUPVAL %d %d) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_GETUPDICT: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(GETUPDICT %d %d %d) ;; line %d\n", a, b, c, instr->ln);
	} break;
	case OP_SETUPDICT: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(SETUPDICT %d %d %d) ;; line %d\n", a, b, c, instr->ln);
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
	case OP_JMP: {
		u64 a = GET_Ax(instr);
		printf("(JMP %lu) ;; line %d\n", a, instr->ln);
	} break;
	case OP_FJMP: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(FJMP %d %lu) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_CALL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(CALL %d %d) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_TAILCALL: {
	} break;
	case OP_RETURN: {
		u8 a = GET_A(instr);
		printf("(RETURN %d) ;; line %d\n", a, instr->ln);
	} break;
	case OP_DICTREF: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(DICTREF %d %d %d) ;; line %d\n", a, b, c, instr->ln);
	} break;
	case OP_DICTSET: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(DICTSET %d %d %d) ;; line %d\n", a, b, c, instr->ln);
	} break;
	case OP_VECREF: {
	} break;
	case OP_VECSET: {
	} break;
	case OP_CLOSURE: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(CLOSURE %d, %lu) ;; line %d\n", a, b, instr->ln);
	} break;
	case OP_DISPLAY: {
		u8 a = GET_A(instr);
		printf("(DISPLAY %d) ;; line %d\n", a, instr->ln);
	} break;
	}
}

void
dis_code(sly_value code)
{
	size_t len = vector_len(code);
	for (size_t i = 0; i < len; ++i) {
		printf("[%zu]\t", i);
		dis(vector_ref(code, i));
	}
}

static void
dis_prototype(prototype *proto)
{
	printf("Disassembly of function @ 0x%lx\n", (uintptr_t)proto);
	dis_code(proto->code);
	printf("============================================\n");
	size_t len = vector_len(proto->K);
	for (size_t i = 0; i < len; ++i) {
		sly_value value = vector_ref(proto->K, i);
		if (prototype_p(value)) {
			dis_prototype(GET_PTR(value));
		}
	}
}

void
dis_all(stack_frame *frame)
{
	printf("Disassembly of top-level code\n");
	dis_code(frame->code);
	printf("============================================\n");
	size_t len = vector_len(frame->K);
	for (size_t i = 0; i < len; ++i) {
		sly_value value = vector_ref(frame->K, i);
		if (prototype_p(value)) {
			dis_prototype(GET_PTR(value));
		}
	}
}
