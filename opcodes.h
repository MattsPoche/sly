#ifndef SLY_OPCODES_H_
#define SLY_OPCODES_H_

enum opcode {
	OP_NOP = 0,
	OP_MOVE,		// iAB  | R[A] := R[B]
	OP_LOADI,		// iABx | R[A] := <i64> Bx
	OP_LOADK,		// iABx | R[A] := K[Bx]
	OP_LOADFALSE,	// iA	| R[A] := #f
	OP_LOADTRUE,	// iA	| R[A] := #t
	OP_LOADNULL,	// iA	| R[A] := '()
	OP_LOADVOID,	// iA	| R[A] := #<void>
	OP_GETUPVAL,	// iAB  | R[A] := U[B]
	OP_SETUPVAL,	// iAB  | U[A] := R[B]
	OP_GETUPDICT,	// iABC | R[A] := <dictionary> U[B][R[C]]
	OP_SETUPDICT,	// iABC | <dictionary> U[A][R[B]] := R[C]
	OP_JMP,			// iAx  | PC := <u64> Ax
	OP_FJMP,		// iABx | if R[A] == #f then PC := Bx
	OP_CALL,		// iAB  | R[A] := (R[A] R[A+1] ... R[A+B-1])
	OP_CALLWCC,     // iAB  | R[A] := (R[B] (current-continuation)) ; call/cc
	OP_TAILCALL,	// iAB  | R[A] := (R[A] R[A+1] ... R[A+B-1])
	OP_RETURN,		// iA   | return R[A]
	OP_CLOSURE,		// iABx | R[A] := make_closure(<prototype> K[Bx])
	OP_COUNT,
};

typedef struct _stack_frame {
	OBJ_HEADER;
	struct _stack_frame *parent;
	sly_value K;    // <vector> constants
	sly_value code; // <vector> byte code
	sly_value U;	// <vector> upvalues
	sly_value R;	// <vector> registers
	sly_value clos; // closure
	size_t pc;		// program counter
	size_t ret_slot;
	u32 level;
} stack_frame;

typedef sly_value INSTR;

union instr {
	INSTR v;
	struct {
		union {
			u8 as_u8[4];
			u16 as_u16[2];
			i16 as_i16[2];
			u32 as_u32;
			i32 as_i32;
		} u;
		i32 ln;
	} i;
};

#define AxMAX   16777215
#define sAxMAX  8388607
#define sAxMIN -8388608
#define BxMAX   UINT16_MAX
#define sBxMAX  INT16_MAX
#define sBxMIN  INT16_MIN
#define REG_MAX UCHAR_MAX
#define UPV_MAX REG_MAX
#define K_MAX   UINT16_MAX

#define GET_OP(instr)   ((enum opcode)((instr).i.u.as_u8[0]))
#define GET_A(instr)    ((instr).i.u.as_u8[1])
#define GET_B(instr)    ((instr).i.u.as_u8[2])
#define GET_C(instr)    ((instr).i.u.as_u8[3])
#define GET_Ax(instr)   ((instr).i.u.as_u32 >> 8)
#define GET_sAx(instr)  ((instr).i.u.as_i32 >> 8)
#define GET_Bx(instr)   ((instr).i.u.as_u16[1])
#define GET_sBx(instr)  ((instr).i.u.as_i16[1])

stack_frame *make_stack(Sly_State *ss, size_t nregs);
stack_frame *make_eval_stack(Sly_State *ss);
void dis(INSTR instr);
void dis_code(sly_value code);
void dis_all(stack_frame *frame, int lstk);

#ifdef OPCODES_INCLUDE_INLINE

static inline INSTR
iA(u8 i, u8 a, int ln)
{
	union instr instr = {0};
	instr.i.u.as_u8[0] = i;
	instr.i.u.as_u8[1] = a;
	instr.i.ln = ln;
	return instr.v;
}

static inline INSTR
iAx(u8 i, u32 ax, int ln)
{
	union instr instr = {0};
	sly_assert(ax <= AxMAX,
			   "Error (op encode) operand outside of accepted range.");
	instr.i.u.as_u32 = (ax & 0x00ffffff) << 8;
	instr.i.u.as_u8[0] = i;
	instr.i.ln = ln;
	return instr.v;
}

static inline INSTR
isAx(u8 i, i32 ax, int ln)
{
	union instr instr = {0};
	sly_assert(ax >= sAxMIN && ax <= sAxMAX,
			   "Error (op encode) operand outside of accepted range.");
	instr.i.u.as_i32 = (ax & 0x00ffffff) << 8;
	instr.i.u.as_u8[0] = i;
	instr.i.ln = ln;
	return instr.v;
}

static inline INSTR
iAB(u8 i, u8 a, u8 b, int ln)
{
	union instr instr = {0};
	instr.i.u.as_u8[0] = i;
	instr.i.u.as_u8[1] = a;
	instr.i.u.as_u8[2] = b;
	instr.i.ln = ln;
	return instr.v;
}


static inline INSTR
iABx(u8 i, u8 a, u16 bx, int ln)
{
	union instr instr = {0};
	instr.i.u.as_u8[0] = i;
	instr.i.u.as_u8[1] = a;
	instr.i.u.as_u16[1] = bx;
	instr.i.ln = ln;
	return instr.v;
}

static inline INSTR
iAsBx(u8 i, u8 a, i16 bx, int ln)
{
	union instr instr = {0};
	instr.i.u.as_u8[0] = i;
	instr.i.u.as_u8[1] = a;
	instr.i.u.as_i16[1] = bx;
	instr.i.ln = ln;
	return instr.v;
}

static inline INSTR
iABC(u8 i, u8 a, u8 b, u8 c, int ln)
{
	union instr instr = {0};
	instr.i.u.as_u8[0] = i;
	instr.i.u.as_u8[1] = a;
	instr.i.u.as_u8[2] = b;
	instr.i.u.as_u8[3] = c;
	instr.i.ln = ln;
	return instr.v;
}

#endif

#endif /* SLY_OPCODES_H_ */
