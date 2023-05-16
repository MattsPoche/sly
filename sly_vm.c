#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"
#include "gc.h"
#include "sly_vm.h"

#define next_instr()    vector_ref(ss->frame->code, ss->frame->pc++)
#define get_const(i)    vector_ref(ss->frame->K, (i))
#define get_reg(i)      vector_ref(ss->frame->R, (i))
#define set_reg(i, v)   vector_set(ss->frame->R, (i), (v))
#define get_upval(i)    upvalue_get(vector_ref(ss->frame->U, (i)))
#define set_upval(i, v) upvalue_set(vector_ref(ss->frame->U, (i)), (v))
#define TOP_LEVEL_P(frame) ((frame)->level == 0 || (frame)->parent == NULL)

static void close_upvalues(Sly_State *ss, stack_frame *frame);
static int funcall(Sly_State *ss, u32 idx, u32 nargs, int as_tailcall);


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

static int
funcall(Sly_State *ss, u32 idx, u32 nargs, int as_tailcall)
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
		//printf("Location: %s:%d\n", ss->file_path, line);
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
		} else {
			sly_assert(nargs == proto->nargs,
					   "Error wrong number of arguments (188)");
		}
		for (size_t i = 0; i < proto->nargs; ++i) {
			vector_set(nframe->R, i, get_reg(a + 1 + i));
		}
		nframe->K = proto->K;
		nframe->code = proto->code;
		nframe->pc = proto->entry;
		if (!TOP_LEVEL_P(ss->frame) && as_tailcall) {
			nframe->ret_slot = ss->frame->ret_slot;
			nframe->level = ss->frame->level;
			nframe->parent = ss->frame->parent;
			close_upvalues(ss, ss->frame);
		} else {
			nframe->ret_slot = a;
			nframe->parent = ss->frame;
			nframe->level = ss->frame->level + 1;
		}
		ss->frame = nframe;
	} else if (continuation_p(val)) {
		/* TODO: continuations should take a vararg?
		 */
		sly_assert(nargs == 1,
				   "Error Wrong number of arguments for continuation");
		continuation *cc = GET_PTR(val);
		sly_value arg = get_reg(a+1);
		ss->frame = cc->frame;
		ss->frame->pc = cc->pc;
		vector_set(ss->frame->R, cc->ret_slot, arg);
	} else {
		dis_all(ss->frame, 1);
		printf("a = %d\n", a);
		sly_display(val, 1);
		printf("\n");
		sly_assert(0, "Type Error expected procedure");
	}
	return TYPEOF(val);
}

static void
close_upvalues(Sly_State *ss, stack_frame *frame)
{
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
vm_run(Sly_State *ss, int run_gc)
{
	sly_value ret_val = SLY_VOID;
	union instr instr;
	if (vector_len(ss->frame->code) == 0) {
		return ret_val;
	}
	//int line = 0;
    for (;;) {
		instr.v = next_instr();
		/* if (instr.i.ln != -1) { */
		/* 	line = instr.i.ln; */
		/* } */
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
		case OP_TAILCALL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			funcall(ss, a, b - a - 1, 1);
		} break;
		case OP_CALL: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			funcall(ss, a, b - a - 1, 0);
		} break;
		case OP_CALLWCC: {
			u8 a = GET_A(instr);
			u8 b = GET_B(instr);
			sly_value proc = get_reg(b);
			sly_value cc = make_continuation(ss, ss->frame, ss->frame->pc, a);
			if (closure_p(proc)) {
				closure *clos = GET_PTR(proc);
				prototype *proto = GET_PTR(clos->proto);
				sly_assert(proto->nargs = 1, "Error wrong number of argument");
				stack_frame *nframe = make_stack(ss, proto->nregs);
				nframe->clos = proc;
				nframe->level = ss->frame->level + 1;
				nframe->U = clos->upvals;
				vector_set(nframe->R, 0, cc);
				nframe->K = proto->K;
				nframe->code = proto->code;
				nframe->pc = proto->entry;
				nframe->ret_slot = a;
				nframe->parent = ss->frame;
				ss->frame = nframe;
			} else {
				sly_assert(0, "CALL/CC Not emplemented for procedure type");
			}
		} break;
		case OP_RETURN: {
			u8 a = GET_A(instr);
			if (TOP_LEVEL_P(ss->frame)) {
				ret_val = get_reg(a);
				goto vm_exit;
			}
			size_t ret_slot = ss->frame->ret_slot;
			ret_val = get_reg(a);
			close_upvalues(ss, ss->frame);
			ss->frame = ss->frame->parent;
			set_reg(ret_slot, ret_val);
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
		if (run_gc) {
			GARBAGE_COLLECT(ss);
		}
	}
vm_exit:
	if (run_gc) {
		GARBAGE_COLLECT(ss);
	}
	return ret_val;
}
