#ifndef SLY_OPCODES_H_
#define SLY_OPCODES_H_

enum opcode {
	OP_NOP = 0,
	OP_MOVE,		// iAB  | R[A] := R[B]
	OP_LOADI,		// iABx | R[A] := <i64> Bx
	OP_LOADK,		// iABx | R[A] := K[Bx]
	OP_LOADFALSE,	// iA	| R[A] := SLY_FALSE
	OP_LOADTRUE,	// iA	| R[A] := SLY_TRUE
	OP_LOADNULL,	// iA	| R[A] := SLY_NULL
	OP_LOADVOID,	// iA	| R[A] := SLY_VOID
	OP_GETUPVAL,	// iAB  | R[A] := U[B]
	OP_SETUPVAL,	// iAB  | U[A] := R[B]
	OP_GETUPDICT,	// iABC | R[A] := <dictionary> U[B][R[C]]
	OP_SETUPDICT,	// iABC | <dictionary> U[A][R[B]] := R[C]
	OP_JMP,			// iAx  | PC := <u64> Ax
	OP_FJMP,		// iABx | if R[A] == #f then PC := Bx
	OP_CALL,		// iAB  | R[A] := (R[A] R[A+1] ... R[A+B-1])
	OP_CALLWCC,     // iA   | R[A] := (R[A] (current-continuation)) ; call/cc
	OP_TAILCALL,	// iAB  | R[A] := (R[A] R[A+1] ... R[A+B-1])
	OP_RETURN,		// iA   | return R[A]
	OP_DICTREF,		// iABC | R[A] := <dictionary> R[B][R[C]]
	OP_DICTSET,		// iABC | <dictionary> R[A][R[B]] := R[C]
	OP_VECREF,		// iABC | R[A] := R[B][R[C]]
	OP_VECSET,		// iABC | R[A][R[B]] = R[C]
	OP_CLOSURE,		// iABx | R[A] := make_closure(<prototype> K[Bx])
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

struct instr {
	u8 b[4];
	i32 ln;
};

typedef sly_value INSTR;

#define AxMAX  0xffffff
#define BxMAX  UINT16_MAX
#define REG_MAX UCHAR_MAX
#define UPV_MAX REG_MAX
#define K_MAX   BxMAX
#define GET_OP(instr)   ((enum opcode)((instr)->b[0]))
#define GET_A(instr)    ((instr)->b[1])
#define GET_B(instr)    ((instr)->b[2])
#define GET_C(instr)    ((instr)->b[3])
#define GET_Ax(instr)   ((u64)(((*((u32 *)(&(instr)->b[0]))) & 0xffffff00) >> 8))
#define GET_Bx(instr)   ((u64)(*((u16 *)(&(instr)->b[2]))))
#define GET_sBx(instr)  ((i64)(*((i16 *)(&(instr)->b[2]))))
#define next_instr()    vector_ref(ss->frame->code, ss->frame->pc++)
#define get_const(i)    vector_ref(ss->frame->K, (i))
#define get_reg(i)      vector_ref(ss->frame->R, (i))
#define set_reg(i, v)   vector_set(ss->frame->R, (i), (v))
#define get_upval(i)    vector_ref(ss->frame->U, (i))
#define set_upval(i, v) vector_set(ss->frame->U, (i), (v))

stack_frame *make_stack(Sly_State *ss, size_t nregs);
void dis(INSTR instr);
void dis_code(sly_value code);
void dis_all(stack_frame *frame, int lstk);

#ifdef OPCODES_INCLUDE_INLINE

static inline INSTR
iA(u8 i, u8 a, int ln)
{
	struct instr instr;
	instr.b[0] = i;
	instr.b[1] = a;
	instr.ln = ln;
	return *((INSTR *)(&instr));
}

static inline INSTR
iAx(u8 i, u32 ax, int ln)
{
	struct instr instr;
	u32 *arg = (u32 *)(&instr.b[0]);
	*arg = (ax & 0x00ffffff) << 8;
	instr.b[0] = i;
	instr.ln = ln;
	return *((INSTR *)(&instr));
}

static inline INSTR
iAB(u8 i, u8 a, u8 b, int ln)
{
	struct instr instr;
	instr.b[0] = i;
	instr.b[1] = a;
	instr.b[2] = b;
	instr.ln = ln;
	return *((INSTR *)(&instr));
}


static inline INSTR
iABx(u8 i, u8 a, u16 bx, int ln)
{
	struct instr instr;
	instr.b[0] = i;
	instr.b[1] = a;
	u16 *arg = (u16 *)(&instr.b[2]);
	*arg = bx;
	instr.ln = ln;
	return *((INSTR *)(&instr));
}

static inline INSTR
iABC(u8 i, u8 a, u8 b, u8 c, int ln)
{
	struct instr instr;
	instr.b[0] = i;
	instr.b[1] = a;
	instr.b[2] = b;
	instr.b[3] = c;
	instr.ln = ln;
	return *((INSTR *)(&instr));
}

#endif

#endif /* SLY_OPCODES_H_ */
