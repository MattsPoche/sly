#define OPCODES_INCLUDE_INLINE 1
#include <gc.h>
#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"
#include "sly_vm.h"
#include "eval.h"
#include "opcodes.h"

#define next_instr()    vector_ref(ss->frame->code, ss->frame->pc++)
#define get_const(i)    vector_ref(ss->frame->K, (i))
#define get_reg(i)      vector_ref(ss->frame->R, (i))
#define set_reg(i, v)   vector_set(ss->frame->R, (i), (v))
#define get_upval(i)    upvalue_get(vector_ref(ss->frame->U, (i)))
#define set_upval(i, v) upvalue_set(vector_ref(ss->frame->U, (i)), (v))
#define TOP_LEVEL_P(frame) ((frame)->level == 0 || (frame)->cont == SLY_NULL)

static void close_upvalues(Sly_State *ss, stack_frame *frame);
static int funcall(Sly_State *ss, u32 idx, u32 nargs, int is_tailcall);

void
vm_bt(stack_frame *frame)
{
	for (;;) {
		closure *clos = GET_PTR(frame->clos);
		printf("Backtrace ...\n");
		printf("pc :: %zu\n", frame->pc);
		dis_prototype(clos->proto, 1);
		if (null_p(frame->cont)) {
			return;
		}
		continuation *cont = GET_PTR(frame->cont);
		frame = cont->frame;
	}
}

sly_value
form_closure(Sly_State *ss, sly_value _proto)
{
	sly_value _clos = make_closure(ss, _proto);
	closure *clos = GET_PTR(_clos);
	prototype *proto = GET_PTR(_proto);
	vector_set(clos->upvals, 0, vector_ref(ss->frame->U, 0));
	size_t len = vector_len(proto->uplist);
	for (size_t i = 0; i < len; ++i) {
		union uplookup upinfo;
		upinfo.v = vector_ref(proto->uplist, i);
		sly_value uv;
		vector *ups =  GET_PTR(ss->frame->U);
		vector *regs = GET_PTR(ss->frame->R);
		if (upinfo.u.isup) {
			uv = ups->elems[upinfo.u.reg];
		} else {
			uv = make_open_upvalue(ss, &regs->elems[upinfo.u.reg]);
		}
		vector_set(clos->upvals, i + 1, uv);
	}
	return _clos;
}

#if 0
static int
is_tailpos(stack_frame *frame, size_t a)
{
	if (TOP_LEVEL_P(frame)) return 0;
	if (null_p(frame->clos)) return 0;
	prototype *p = GET_PTR(get_prototype(frame->clos));
	return a == p->nvars + 1
		&& vector_ref(frame->R, p->nvars) == frame->cont;
}
#endif

static void
vector_force(Sly_State *ss, sly_value v, size_t idx, sly_value value)
{
	if (idx < vector_len(v)) {
		vector_set(v, idx, value);
	} else {
		vector_append(ss, v, value);
	}
}

static int
funcall(Sly_State *ss, u32 idx, u32 nargs, int is_tailpos)
{
	int a = idx;
	int b = idx + nargs + 1;
	sly_value val = get_reg(a);
	if (cclosure_p(val)) {
		cclosure *clos = GET_PTR(val);
		sly_value args;
		if (clos->has_varg) {
			sly_assert(nargs >= clos->nargs,
					   "Error wrong number of arguments (152)");
			size_t nvargs = nargs - clos->nargs;
			sly_value vargs = SLY_NULL;
			args = make_vector(ss, clos->nargs + 1, clos->nargs + 1);
			for (size_t i = b - 1; nvargs--; --i) {
				vargs = cons(ss, get_reg(i), vargs);
			}
			vector_set(args, clos->nargs, vargs);
			nargs = clos->nargs;
		} else {
			if (nargs != clos->nargs) {
				sly_display(ss->frame->clos, 1);
				printf("\n");
				sly_display(val, 1);
				printf("\n");
				sly_assert(0, "Error wrong number of arguments (163)");
			}
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
		nframe->U = clos->upvals;
		if (proto->has_varg) {
			sly_assert(nargs >= proto->nargs,
					   "Error wrong number of arguments (179)");
			size_t nvargs = nargs - proto->nargs;
			sly_value vargs = SLY_NULL;
			for (size_t i = b - 1; nvargs--; --i) {
				vargs = cons(ss, get_reg(i), vargs);
			}
			vector_set(nframe->R, proto->nargs, vargs);
		} else if (nargs != proto->nargs) {
			printf("nargs :: %u\n", nargs);
			printf("expected :: %zu\n", proto->nargs);
			closure *fc = GET_PTR(ss->frame->clos);
			dis_prototype(fc->proto, 1);
			dis_prototype(clos->proto, 1);
			printf("\npc: %zu\n", ss->frame->pc);
			sly_assert(0, "Error wrong number of arguments (188)");
		}
		for (size_t i = 0; i < proto->nargs; ++i) {
			vector_set(nframe->R, i, get_reg(a + 1 + i));
		}
		nframe->K = proto->K;
		nframe->code = proto->code;
		nframe->pc = proto->entry;
		if (!TOP_LEVEL_P(ss->frame) && is_tailpos) {
			nframe->level = ss->frame->level;
			nframe->cont = ss->frame->cont;
			close_upvalues(ss, ss->frame);
		} else {
			nframe->cont = make_continuation(ss, ss->frame, ss->frame->pc, a);
			nframe->level = ss->frame->level + 1;
		}
		ss->frame = nframe;
	} else if (continuation_p(val)) {
		continuation *cc = GET_PTR(val);
		sly_value arg;
		int i;
		for (i = 0; i < cc->nargs; ++i) {
			arg = get_reg(a + 1 + i);
			vector_force(ss, cc->frame->R, cc->ret_slot + i, arg);
		}
		if (cc->has_varg) {
			sly_value list = SLY_NULL;
			for (int j = nargs - 1; i <= j; --j) {
				list = cons(ss, get_reg(a + 1 + j), list);
			}
			vector_force(ss, cc->frame->R, i, list);
		}
		if (ss->frame->cont == val) {
			close_upvalues(ss, ss->frame);
		} else if (!TOP_LEVEL_P(ss->frame) && is_tailpos) {
			cc->frame->cont = ss->frame->cont;
		} else {
			cc->frame->cont = make_continuation(ss, ss->frame, ss->frame->pc, a);
		}
		close_upvalues(ss, ss->frame);
		ss->frame = cc->frame;
		ss->frame->pc = cc->pc;
	} else {
		printf("pc :: %zu\n", ss->frame->pc);
		sly_displayln(val);
		sly_displayln(ss->frame->clos);
		vm_bt(ss->frame);
		sly_assert(0, "Type Error expected procedure");
	}
	return TYPEOF(val);
}

static void
close_upvalues(Sly_State *ss, stack_frame *frame)
{
	if (null_p(frame->clos)) return;
	vector *vec = GET_PTR(frame->R);
	upvalue *uv = NULL;
	upvalue *parent = NULL;
	closure *clos = GET_PTR(frame->clos);
	prototype *proto = GET_PTR(clos->proto);
	size_t nvars = proto->nvars + proto->has_varg;
	for (size_t i = 0; i < nvars; ++i) {
		uv = find_open_upvalue(ss, &vec->elems[i], &parent);
		if (uv) {
			if (parent == NULL) {
				ss->open_upvals = uv->next;
			} else {
				parent->next = uv->next;
			}
			close_upvalue((sly_value)uv);
		}
	}
}

sly_value
vm_run(Sly_State *ss)
{
	sly_value ret_val = SLY_VOID;
	union instr instr;
	int is_tailpos = 0;
	if (vector_len(ss->frame->code) == 0) {
		return ret_val;
	}
    for (;;) {
		instr.v = next_instr();
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
			sly_value val = get_const(b);
			if (pair_p(val)) {
				val = copy_list(ss, val);
			}
			set_reg(a, val);
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
		case OP_LOADCONT: {
			u8 a = GET_A(instr);
			set_reg(a, ss->frame->cont);
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
			dictionary_set(ss, dict, get_reg(b), get_reg(c));
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
		case OP_JMP: {
			u64 a = GET_Ax(instr);
			ss->frame->pc = a;
		} break;
		case OP_FJMP: {
			u8 a = GET_A(instr);
			u64 b = GET_Bx(instr);
			if (get_reg(a) == SLY_FALSE) {
				ss->frame->pc = b;
			}
		} break;
		case OP_CALL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			if (null_p(get_reg(a))) {
				return get_reg(a+1);
			}
			funcall(ss, a, b - a - 1, 0);
		} break;
		case OP_TAILCALL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			if (null_p(get_reg(a))) {
				return get_reg(a+1);
			}
			funcall(ss, a, b - a - 1, 1);
		} break;
		case OP_CALLWCC: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value proc = get_reg(b);
			if (closure_p(proc)) {
				closure *clos = GET_PTR(proc);
				prototype *proto = GET_PTR(clos->proto);
				sly_assert(proto->nargs = 1, "Error wrong number of argument");
				stack_frame *nframe = make_stack(ss, proto->nregs);
				nframe->clos = proc;
				nframe->U = clos->upvals;
				nframe->K = proto->K;
				nframe->code = proto->code;
				nframe->pc = proto->entry;
				sly_value cc;
				if (!TOP_LEVEL_P(ss->frame) && c) {
					nframe->level = ss->frame->level;
					cc = ss->frame->cont;
					close_upvalues(ss, ss->frame);
				} else {
					cc = make_continuation(ss, ss->frame, ss->frame->pc, a);
					nframe->level = ss->frame->level + 1;
				}
				nframe->cont = cc;
				vector_set(nframe->R, 0, cc);
				ss->frame = nframe;
			} else {
				sly_assert(0, "CALL/CC Not implemented for procedure type");
			}
		} break;
		case OP_CALLWVALUES0:
			is_tailpos = 1;
			__attribute__((fallthrough));
		case OP_CALLWVALUES: {
			/* TODO: appears to be working. Needs more testing.
			 */
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value producer = get_reg(b);
			sly_value receiver = get_reg(c);
			/* TODO: Make this work with builtins and continuations
			 */
			sly_assert(closure_p(receiver), "Type Error expected <closure>");
			closure *clos = GET_PTR(receiver);
			prototype *rproto = GET_PTR(clos->proto);
			stack_frame *rframe = make_stack(ss, rproto->nregs);
			rframe->clos = receiver;
			rframe->U = clos->upvals;
			rframe->K = rproto->K;
			rframe->code = rproto->code;
			rframe->pc = rproto->entry;
			if (!TOP_LEVEL_P(ss->frame) && is_tailpos) {
				rframe->level = ss->frame->level;
				rframe->cont = ss->frame->cont;
				close_upvalues(ss, ss->frame);
			} else {
				rframe->cont = make_continuation(ss, ss->frame, ss->frame->pc, a);
				rframe->level = ss->frame->level + 1;
			}
			is_tailpos = 0;
			clos = GET_PTR(producer);
			prototype *pproto = GET_PTR(clos->proto);
			stack_frame *pframe = make_stack(ss, pproto->nregs);
			pframe->clos = producer;
			pframe->U = clos->upvals;
			pframe->K = pproto->K;
			pframe->code = pproto->code;
			pframe->pc = pproto->entry;
			pframe->level = rframe->level + 1;
			pframe->cont = make_continuation(ss, rframe, rframe->pc, 0);
			continuation *cc = GET_PTR(pframe->cont);
			cc->nargs = rproto->nargs;
			cc->has_varg = rproto->has_varg;
			ss->frame = pframe;
		} break;
		case OP_APPLY: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			u8 c = GET_C(instr);
			sly_value args = make_vector(ss, 0, 8);
			vector_append(ss, args, SLY_NULL);
			int nargs = 0;
			for (int i = a; i < b - 1; ++i) {
				vector_append(ss, args, vector_ref(ss->frame->R, i));
				nargs++;
			}
			sly_value list = vector_ref(ss->frame->R, b - 1);
			sly_assert(pair_p(list) || null_p(list), "Error Expected list");
			while (!null_p(list)) {
				vector_append(ss, args, car(list));
				list = cdr(list);
				nargs++;
			}
			stack_frame *nframe = make_eval_stack(ss, args);
			vector_append(ss, nframe->code, iA(OP_LOADCONT, 0, -1));
			vector_append(ss, nframe->code, iAB(OP_TAILCALL, 1, nargs + 1, -1));
			vector_append(ss, nframe->code, iAB(OP_TAILCALL, 0, 1, -1));
			if (!TOP_LEVEL_P(ss->frame) && c) {
				nframe->level = ss->frame->level;
				nframe->cont = ss->frame->cont;
				close_upvalues(ss, ss->frame);
			} else {
				nframe->cont = make_continuation(ss, ss->frame, ss->frame->pc, a);
				nframe->level = ss->frame->level + 1;
			}
			ss->frame = nframe;
		} break;
		case OP_EXIT: {
			u8 a = GET_A(instr);
			//u8 b = GET_B(instr);
			if (TOP_LEVEL_P(ss->frame)) {
				return get_reg(a);
			} else {
				sly_assert(0, "Exit from non toplevel");
			}
		} break;
		case OP_CLOSURE: {
			u8 a = GET_A(instr);
			size_t b = GET_Bx(instr);
			sly_value _proto = get_const(b);
			sly_value clos = form_closure(ss, _proto);
			set_reg(a, clos);
		} break;
		case OP_COUNT:
		default: {
			sly_assert(0, "Error invalid opcode");
		} break;
		}
	}
	return ret_val;
}
