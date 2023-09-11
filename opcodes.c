#include <ctype.h>
#include "sly_types.h"
#include "opcodes.h"
#include "sly_alloc.h"

stack_frame *
make_stack(Sly_State *ss, size_t nregs)
{
	if (nregs >= REG_MAX) {
		sly_raise_exception(ss, EXC_ALLOC, "Stack too big");
	}
	stack_frame *frame = gc_alloc(ss, sizeof(*frame));
	frame->h.type = tt_stack_frame;
	frame->cont = SLY_NULL;
	frame->R = make_vector(ss, nregs*2, nregs*2);
	for (size_t i = 0; i < nregs; ++i) {
		vector_set(frame->R, i, SLY_VOID);
	}
	return frame;
}

void
dis(INSTR ins, sly_value si)
{
	union instr instr;
	instr.v = ins;
	enum opcode op = GET_OP(instr);
	int pad = 0;
	switch (op) {
	case OP_NOP: {
		printf("(NOP)%n", &pad);
	} break;
	case OP_MOVE: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(MOVE %d %d)%n", a, b, &pad);
	} break;
	case OP_LOADI: {
		u8 a = GET_A(instr);
		i64 b = GET_sBx(instr);
		printf("(LOADI %d %ld)%n", a, b, &pad);
	} break;
	case OP_LOADK: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(LOADK %d %lu)%n", a, b, &pad);
	} break;
	case OP_LOADFALSE: {
		u8 a = GET_A(instr);
		printf("(LOADFALSE %d)%n", a, &pad);
	} break;
	case OP_LOADTRUE: {
		u8 a = GET_A(instr);
		printf("(LOADTRUE %d)%n", a, &pad);
	} break;
	case OP_LOADNULL: {
		u8 a = GET_A(instr);
		printf("(LOADNULL %d)%n", a, &pad);
	} break;
	case OP_LOADVOID: {
		u8 a = GET_A(instr);
		printf("(LOADVOID %d)%n", a, &pad);
	} break;
	case OP_LOADCONT: {
		u8 a = GET_A(instr);
		printf("(LOADCONT %d)%n", a, &pad);
	} break;
	case OP_GETUPVAL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(GETUPVAL %d %d)%n", a, b, &pad);
	} break;
	case OP_SETUPVAL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(SETUPVAL %d %d)%n", a, b, &pad);
	} break;
	case OP_GETUPDICT: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(GETUPDICT %d %d %d)%n", a, b, c, &pad);
	} break;
	case OP_SETUPDICT: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(SETUPDICT %d %d %d)%n", a, b, c, &pad);
	} break;
	case OP_DICTREF: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(DICTREF %d %d %d)%n", a, b, c, &pad);
	} break;
	case OP_DICTSET: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(DICTSET %d %d %d)%n", a, b, c, &pad);
	} break;
	case OP_JMP: {
		u64 a = GET_Ax(instr);
		printf("(JMP %lu)%n", a, &pad);
	} break;
	case OP_FJMP: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(FJMP %d %lu)%n", a, b, &pad);
	} break;
	case OP_CALL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(CALL %d %d)%n", a, b, &pad);
	} break;
	case OP_TAILCALL: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(TAILCALL %d %d)%n", a, b, &pad);
	} break;
	case OP_CALLWVALUES0:
	case OP_CALLWVALUES: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		u8 c = GET_C(instr);
		printf("(CALL/VALUES %d %d %d)%n", a, b, c, &pad);
	} break;
	case OP_APPLY: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(APPLY %d %d)%n", a, b, &pad);
	} break;
	case OP_CALLWCC: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(CALL/CC %d %d)%n", a, b, &pad);
	} break;
	case OP_EXIT: {
		u8 a = GET_A(instr);
		u8 b = GET_B(instr);
		printf("(EXIT %d %d)%n", a, b, &pad);
	} break;
	case OP_CLOSURE: {
		u8 a = GET_A(instr);
		u64 b = GET_Bx(instr);
		printf("(CLOSURE %d %lu)%n", a, b, &pad);
	} break;
	case OP_COUNT:
	default: {
		sly_assert(0, "Error invalid opcode");
	} break;
	}
	if (instr.i.ln != -1 && vector_p(si)) {
		syntax *s = GET_PTR(vector_ref(si, instr.i.ln));
		token t = s->tok;
		printf("%*s", 20 - pad, "");
		printf(";; %d:%d%n", t.ln + 1, t.cn, &pad);
		if (t.src) {
			int start = t.so;
			int end = t.eo;
			while (start > 0) {
				if (t.src[start] == '\n') {
					while (isspace((int)t.src[start])) start++;
					break;
				}
				if (t.src[start] == '(') {
					break;
				}
				start--;
			}
			int bal = 0;
			while (t.src[end] != '\0') {
				if (t.src[end] == '\n') {
					break;
				}
				if (t.src[end] == ')') {
					if (bal == 0) {
						end++;
						break;
					}
					bal--;
				}
				if (t.src[end] == '(') {
					bal++;
				}
				end++;
			}
#if 0
			char red[] = "\x1b[1;31m";
			char color_end[] = "\x1b[0m";
#else
			char red[] = "";
			char color_end[] = "";
#endif
			printf("%*s%.*s%s", 14 - pad, "", t.so - start, &t.src[start], red);
			printf("%.*s", t.eo - t.so, &t.src[t.so]);
			printf("%s%.*s", color_end, end - t.eo, &t.src[t.eo]);
		}
	}
	printf("\n");
}

void
dis_code(sly_value code, sly_value si)
{
	size_t len = vector_len(code);
	int pad;
	for (size_t i = 0; i < len; ++i) {
		printf("[%zu]%n", i, &pad);
		printf("%*s", 10 - pad, "");
		dis(vector_ref(code, i), si);
	}
}

static void
_dis_prototype(prototype *proto, int lstk)
{
	printf("Disassembly of function @ 0x%lx\n", (uintptr_t)proto);
	dis_code(proto->code, proto->syntax_info);
	if (lstk) {
		size_t len = vector_len(proto->K);
		size_t ulen = vector_len(proto->uplist);
		printf("Regs: %zu\n", proto->nregs);
		printf("Vars: %zu\n", proto->nvars);
		printf("Args: %zu\n", proto->nargs);
		printf("Upvalues: %zu\n", vector_len(proto->uplist));
		printf("Upinfo\n");
		union uplookup uv;
		for (size_t i = 0; i < ulen; ++i) {
			uv.v = vector_ref(proto->uplist, i);
			printf("%-10zu%d; %d\n", i, uv.u.isup, uv.u.reg);
		}
		printf("Constants\n");
		for (size_t i = 0; i < len; ++i) {
			sly_value k = vector_ref(proto->K, i);
			printf("%-10zu", i);
			if (symbol_p(k)) {
				sly_value alias = symbol_get_alias(k);
				if (null_p(alias)) {
					sly_display(k, 1);
					printf("\n");
				} else {
					int pad;
					printf("%s%n", symbol_to_cstr(alias), &pad);
					printf("%*s->%*s%s\n",
						   12 - pad, "",
						   8, "",
						   symbol_to_cstr(k));
				}
			} else {
				sly_display(k, 1);
				printf("\n");
			}
		}
	}
}

void
dis_prototype(sly_value proto, int lstk)
{
	_dis_prototype(GET_PTR(proto), lstk);
}

void
dis_prototype_rec(sly_value p, int lstk)
{
	prototype *proto = GET_PTR(p);
	_dis_prototype(proto, lstk);
	printf("============================================\n");
	size_t len = vector_len(proto->K);
	for (size_t i = 0; i < len; ++i) {
		sly_value value = vector_ref(proto->K, i);
		if (prototype_p(value)) {
			dis_prototype(value, lstk);
		}
	}
}

void
dis_all(stack_frame *frame, int lstk)
{
	sly_value proto = SLY_NULL;
	if (closure_p(frame->clos)) {
		closure *clos = GET_PTR(frame->clos);
		proto = clos->proto;
	}
	prototype *_proto = GET_PTR(proto);
	printf("Disassembly of top-level code ");
	sly_display(proto, 1);
	printf("\n");
	dis_code(frame->code, _proto->syntax_info);
	size_t len = vector_len(frame->K);
	if (lstk) {
		printf("Constants\n");
		for (size_t i = 0; i < len; ++i) {
			printf("%-10zu", i);
			sly_display(vector_ref(frame->K, i), 1);
			printf("\n");
		}
	}
	printf("============================================\n");
	for (size_t i = 0; i < len; ++i) {
		sly_value value = vector_ref(frame->K, i);
		if (prototype_p(value)) {
			dis_prototype_rec(value, lstk);
		}
	}
}
