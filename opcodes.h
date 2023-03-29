#ifndef SLY_OPCODES_H_
#define SLY_OPCODES_H_

/*
64 bit instruction
| 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 |
|      G          |        F        |      E          |      D          |       C         |      B          |      A          |      OP CODE    |
|            Dx                                                         |                 |                 |                 |      OP CODE    |
|            Bx                                                                                             |                 |                 |
*/
#define REG_MAX UCHAR_MAX
#define OP_MASK 0xFF
#define A_MASK  (0xFF << 8)
#define B_MASK  (0xFF << 16)
#define C_MASK  (0xFF << 24)
#define D_MASK  (0xFF << 32)
#define E_MASK  (0xFF << 40)
#define F_MASK  (0xFF << 48)
#define G_MASK  (0xFF << 56)
#define Ax_MASK (~OP_MASK)
#define Bx_MASK (~(A_MASK|OP_MASK))
#define Cx_MASK (~(B_MASK|A_MASK|OP_MASK))
#define Dx_MASK (~(C_MASK|B_MASK|OP_MASK))
#define GET_OP(instr) ((enum opcode)((instr) & OP_MASK))
#define GET_A(instr)  ((u8)(((instr) & A_MASK) >> 8))
#define GET_B(instr)  ((u8)(((instr) & B_MASK) >> 16))
#define GET_C(instr)  ((u8)(((instr) & C_MASK) >> 24))
#define GET_D(instr)  ((u8)(((instr) & D_MASK) >> 32))
#define GET_E(instr)  ((u8)(((instr) & E_MASK) >> 40))
#define GET_F(instr)  ((u8)(((instr) & F_MASK) >> 48))
#define GET_G(instr)  ((u8)(((instr) & G_MASK) >> 56))
#define GET_Ax(instr) ((sly_value)(((instr) & Bx_MASK) >> 8))
#define GET_Bx(instr) ((sly_value)(((instr) & Bx_MASK) >> 16))
#define GET_Cx(instr) ((sly_value)(((instr) & Cx_MASK) >> 24))
#define GET_Dx(instr) ((sly_value)(((instr) & Dx_MASK) >> 32))
#define iA(i, a)         (((a) << 8)|(i))
#define iAx(i, a)        iA(i, a)
#define iAB(i, a, b)     (((b) << 16)|((a) << 8)|(i))
#define iABx(i, a, b)    iAB(i, a, b)
#define iADx(i, a, d)    (((d) << 32)|((a) << 8)|(i))
#define iABC(i, a, b, c) (((c) << 24)|((b) << 16)|((a) << 8)|(i))
#define next_instr()     vector_ref(frame->code, frame->pc++)
#define get_const(i)    vector_ref(frame->K, (i))
#define get_reg(i)      vector_ref(frame->R, (i))
#define set_reg(i, v)   vector_set(frame->R, (i), (v))
#define get_upval(i) vector_ref(frame->U, (i))
#define set_upval(i, v) vector_set(frame->U, (i), (v))

enum opcode {
	OP_NOP = 0,
	OP_MOVE,      // iAB   | R[A] := R[B]
	OP_LOADI,    // iABx  | R[A] := <i64> Bx
	OP_LOADK,    // iABx  | R[A] := K[Bx]
	OP_GETUPVAL, // iAB  | R[A] := U[B]
	OP_SETUPVAL, // iAB  | U[B] := R[A]
	OP_GETUPDICT, // iABC | R[A] := <dictionary> U[B][R[C]]
	OP_SETUPDICT, // iABC | <dictionary> U[A][R[B]] := R[C]
	OP_CONS,     // iABC  | R[A] := pair(R[B], R[C])
	OP_CAR,      // iAB   | R[A] := car(R[B])
	OP_CDR,      // iAB   | R[A] := cdr(R[B])
	OP_SETCAR,  // iAB   | CAR(R[A]) := R[B]
	OP_SETCDR,  // iAB   | CDR(R[A]) := R[B]
	OP_ADD,     // iABC  | R[C] := <i64> R[A] + <i64> R[B]
	OP_SUB,     // iABC  | R[C] := <i64> R[A] - <i64> R[B]
	OP_MUL,     // iABC  | R[C] := <i64> R[A] * <i64> R[B]
	OP_DIV,     // iABC  | R[C] := <i64> R[A] / <i64> R[B]
	OP_MOD,     // iABC  | R[C] := <i64> R[A] % <i64> R[B]
	OP_JMP,     // iAx   | PC := <u64> Ax
	OP_JGZ,     // iABx  | if <i64> R[A] > 0: PC := <u64> Bx
	OP_JLZ,     // iABx  | if <i64> R[A] < 0: PC := <u64> Bx
	OP_JEZ,     // iABx  | if <i64> R[A] == 0: PC := <u64> Bx
	OP_JNZ,     // iABx  | if <i64> R[A] != 0: PC := <u64> Bx
	OP_CALL,    // iAB   | R[A] := R[A](A+1, ..., A+B-1)
	OP_TAILCALL, // iAB   | R[A] := R[A](A+1, ..., A+B-1)
	OP_RETURN,  // iA    | return R[A]
	OP_DICTREF, // iABC   | R[A] := <dictionary> R[B][R[C]]
	OP_DICTSET, // iABC  | <dictionary> R[A][R[B]] := R[C]
	OP_VECREF,  // iABC  | R[A] := R[B][R[C]]
	OP_VECSET,  // iABC  | R[A][R[B]] = R[C]
	OP_CLOSURE, // iABx  | R[A] := make_closure(<prototype> K[Bx])
	OP_DISPLAY, // iA    | (display R[A])
};

typedef struct _stack_frame {
	OBJ_HEADER;
	struct _stack_frame *parent;
	sly_value K;    // <vector> constants
	sly_value code; // <vector> byte code
	sly_value U;  // <vector> upvalues
	sly_value R;  // <vector> registers
	size_t pc;    // program counter
	size_t ret_slot;
} stack_frame;

typedef sly_value INSTR;

static inline stack_frame *
make_stack(size_t nregs)
{
	sly_assert(nregs <= REG_MAX, "Stack too big");
	stack_frame *frame = sly_alloc(sizeof(*frame));
	frame->parent = NULL;
	frame->R = make_vector(nregs, nregs);
	return frame;
}

#endif /* SLY_OPCODES_H_ */
