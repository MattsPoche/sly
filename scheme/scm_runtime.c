#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../common/common_def.h"
#include "scm_types.h"
#include "scm_runtime.h"

#define ARG_STACK_LEN 0xffLU
static struct {
	size_t top;
	scm_value stk[ARG_STACK_LEN];
} arg_stack;

#define module_entry(name, fn) _cons((scm_value)(name), make_function(fn))

#define HEAP_PAGE_SZ (1LU << 12)
static Mem_Pool mp0 = {0};
static Mem_Pool mp1 = {0};
static Mem_Pool *heap_working = &mp0;
static Mem_Pool *heap_free = &mp1;
static size_t gc_threashold = 0;
static size_t bytes_allocated = 0;
static size_t gc_cycles = 0;
static jmp_buf exit_point;
static scm_value exception_handler;
static scm_value **intern_tbl;
static size_t intern_tbl_len = 0;
static scm_value *interned;
static struct constant *constants;

scm_value
_tail_call(scm_value proc)
{
	if (BOX_P(proc)) {
		proc = box_ref(proc);
	}
	if (!PROCEDURE_P(proc)) {
		printf("\n#x%lx\n", proc);
		scm_assert(PROCEDURE_P(proc), "type error, expected procedure");
	}
	if (arg_stack.top == 0) {
		push_arg(SCM_VOID);
	}
	return proc;
}

void
scm_heap_init(void)
{
	mp0.sz = HEAP_PAGE_SZ;
	mp0.buf = malloc(HEAP_PAGE_SZ);
	scm_assert(mp0.buf != NULL, "OS is too greedy :(");
	mp0.idx = sizeof(scm_value);
}

size_t
mem_align_offset(size_t addr)
{
	return ((~addr) + 1) & 0x7;
}

size_t
scm_heap_alloc(size_t sz)
{
	sz += mem_align_offset(sz);
	if (sz >= (heap_working->sz - heap_working->idx)) {
		heap_working->sz *= 2;
		heap_working->buf = realloc(heap_working->buf,
									heap_working->sz);
	}
	size_t i = heap_working->idx;
	heap_working->idx += sz;
	bytes_allocated += sz;
	return i;
}

static void
scm_collect_value(scm_value *vptr)
{
	if (NUMBER_P(*vptr) || BOOLEAN_P(*vptr)
		|| CHAR_P(*vptr) || NULL_P(*vptr)
		|| VOID_P(*vptr) || FUNCTION_P(*vptr)) {
		return;
	}
	u32 fref;
	if ((fref = *((u32 *)GET_WORKING_PTR(*vptr)))) {
		*vptr = (*vptr ^ (*vptr & ((1LU << 32) - 1))) | fref;
		return;
	}
	size_t sz = 0;
	fref = heap_free->idx;
	switch ((enum type_tag)(TYPEOF(*vptr) & 0xf)) {
	case tt_bigint: {
		scm_assert(0, "unimplemented");
	} break;
	case tt_pair: {
		Pair *p = GET_WORKING_PTR(*vptr);
		sz = sizeof(*p);
		heap_free->idx += sz;
		scm_collect_value(&p->car);
		scm_collect_value(&p->cdr);
	} break;
	case tt_symbol: {
		Symbol *s = GET_WORKING_PTR(*vptr);
		sz = sizeof(*s) + s->len;
		sz += mem_align_offset(sz);
		heap_free->idx += sz;
	} break;
	case tt_bytevector: {
		Bytevector *b = GET_WORKING_PTR(*vptr);
		sz = sizeof(*b) + b->len;
		sz += mem_align_offset(sz);
		heap_free->idx += sz;
	} break;
	case tt_string: {
		String *s = GET_WORKING_PTR(*vptr);
		sz = sizeof(*s) + s->len;
		sz += mem_align_offset(sz);
		heap_free->idx += sz;
	} break;
	case tt_vector: {
		Vector *v = GET_WORKING_PTR(*vptr);
		sz = sizeof(*v) + (v->len * sizeof(scm_value));
		heap_free->idx += sz;
		for (u32 i = 0; i < v->len; ++i) {
			scm_collect_value(&v->elems[i]);
		}
	} break;
	case tt_record: {
		Record *rec = GET_WORKING_PTR(*vptr);
		sz = sizeof(*rec) + (rec->len * sizeof(scm_value));
		heap_free->idx += sz;
		scm_collect_value(&rec->type);
		for (u32 i = 0; i < rec->len; ++i) {
			scm_collect_value(&rec->elems[i]);
		}
	} break;
	case tt_box: {
		Box *box = GET_WORKING_PTR(*vptr);
		sz = sizeof(*box);
		heap_free->idx += sz;
		scm_collect_value(&box->value);
	} break;
	case tt_closure: {
		Closure *clos = GET_WORKING_PTR(*vptr);
		sz = sizeof(*clos) + (clos->nfree_vars * sizeof(scm_value));
		heap_free->idx += sz;
		for (u32 i = 0; i < clos->nfree_vars; ++i) {
			scm_collect_value(&clos->free_vars[i]);
		}
	} break;
	case tt_inf:
	case tt_void:
	case tt_bool:
	case tt_char:
	case tt_int:
	case tt_function:
	case TT_COUNT:
	case tt_float:
	default: {
		scm_assert(0, "Unreachable");
	} break;
	}
	memcpy(&heap_free->buf[fref],
		   GET_WORKING_PTR(*vptr),
		   sz);
	*((u32 *)GET_WORKING_PTR(*vptr)) = fref;
	*vptr = (*vptr ^ (*vptr & ((1LU << 32) - 1))) | fref;
}

static void
scm_gc(scm_value *cc)
{
	heap_free->idx = sizeof(scm_value);
	heap_free->sz = heap_working->sz;
	heap_free->buf = malloc(heap_free->sz);
	for (size_t i = 0; i < intern_tbl_len; ++i) {
		scm_value *interned = intern_tbl[i];
		size_t len = interned[0];
		for (size_t j = 1; j < len+1; ++j) {
			scm_collect_value(&interned[j]);
		}
	}
	for (size_t i = 0; i < arg_stack.top; ++i) {
		scm_collect_value(&arg_stack.stk[i]);
	}
	scm_collect_value(cc);
	void *tmp = heap_working;
	heap_working = heap_free;
	heap_free = tmp;
	free(heap_free->buf);
	heap_free->buf = NULL;
	heap_free->idx = 0;
	heap_free->sz = 0;
	gc_threashold = heap_working->idx;
}

void
scm_chk_heap(scm_value *cc)
{
	if (gc_threashold == 0) {
		gc_threashold = heap_working->idx;
	} else if (heap_working->idx > (gc_threashold + (gc_threashold / 2))) {
		gc_cycles++;
		scm_gc(cc);
	}
}

void
_scm_assert(int p, char *msg, const char *func_name)
{
	if (!p) {
		fprintf(stderr, "Error: (%s) %s\n", func_name, msg);
		Closure *c = GET_PTR(exception_handler);
		c->code(exception_handler);
	}
}

int
chk_args(size_t req, int rest)
{
	size_t nargs = arg_stack.top;
	if (rest) {
		return nargs >= req;
	} else {
		return nargs == req;
	}
}

scm_value
cons_rest(void)
{
	scm_value rest = SCM_NULL;
	for (size_t i = 0; i < arg_stack.top; ++i) {
		rest = _cons(arg_stack.stk[i], rest);
	}
	arg_stack.top = 0;
	return rest;
}

scm_value
_cons(scm_value car, scm_value cdr)
{
	scm_value value = scm_heap_alloc(sizeof(Pair));
	Pair *p = GET_PTR(value);
	p->fref = 0;
	p->car = car;
	p->cdr = cdr;
	return (NB_PAIR << 48)|value;
}

scm_value
init_constant(struct constant cnst)
{
	scm_value v = SCM_VOID;
	switch (cnst.tt) {
	case tt_inf: {
		scm_assert(0, "Invalid type");
		v = make_float(cnst.u.as_float);
	} break;
	case tt_bool: {
		v = ITOBOOL(cnst.u.as_int);
	} break;
	case tt_char: {
		v = make_char(cnst.u.as_int);
	} break;
	case tt_int: {
		v = make_int(cnst.u.as_int);
	} break;
	case tt_float: {
		v = make_float(cnst.u.as_float);
	} break;
	case tt_pair: {
		if (cnst.u.as_ptr == NULL) {
			v = SCM_NULL;
		} else {
			v = init_list(cnst.u.as_ptr);
		}
	} break;
	case tt_symbol: {
		v = init_symbol(cnst.u.as_ptr);
	} break;
	case tt_bytevector: {
		v = init_bytevector(cnst.u.as_ptr);
	} break;
	case tt_string: {
		v = init_string(cnst.u.as_ptr);
	} break;
	case tt_vector: {
		v = init_vector(cnst.u.as_ptr);
	} break;
	case tt_box: {
		v = make_constant(cnst.u.as_uint);
	} break;
	case tt_void: {
		v = SCM_VOID;
	} break;
	case tt_record:
	case tt_closure:
	case tt_bigint:
	case tt_function: {
		scm_assert(0, "unimplemented");
	} break;
	case TT_COUNT:
	default: {
		scm_assert(0, "Invalid type");
	} break;
	}
	return v;
}

scm_value
make_constant(size_t idx)
{
	scm_value c;
	if ((c = interned[idx])) {
		return c;
	}
	c = init_constant(constants[idx-1]);
	interned[idx] = c;
	return c;
}

scm_value
load_interned_constant(size_t idx)
{
	return interned[idx];
}

scm_value *
scm_intern_constants(struct constant *c, size_t len)
{
	interned = calloc(len+1, sizeof(*interned));
	interned[0] = len;
	constants = c;
	intern_tbl_len++;
	intern_tbl = realloc(intern_tbl, sizeof(*intern_tbl) * intern_tbl_len);
	intern_tbl[intern_tbl_len-1] = interned;
	for (size_t i = 1; i < len+1; ++i) {
		interned[i] = make_constant(i);
	}
	return interned+1;
}

scm_value
init_string(const STATIC_String *stc_str)
{
	String *s;
	scm_value value = scm_heap_alloc(sizeof(*s) + stc_str->len);
	s = GET_PTR(value);
	s->fref = 0;
	s->len = stc_str->len;
	memcpy(s->elems, stc_str->elems, stc_str->len);
	return (NB_STRING << 48)|value;
}

scm_value
init_symbol(const STATIC_Symbol *stc_sym)
{
	Symbol *s;
	scm_value value = scm_heap_alloc(sizeof(*s) + stc_sym->len);
	s = GET_PTR(value);
	s->fref = 0;
	s->hash = stc_sym->hash;
	s->len = stc_sym->len;
	memcpy(s->name, stc_sym->elems, stc_sym->len);
	return (NB_SYMBOL << 48)|value;
}

scm_value
init_bytevector(const STATIC_Bytevector *stc_vu8)
{
	Bytevector *v;
	scm_value value = scm_heap_alloc(sizeof(*v) + stc_vu8->len);
	v = GET_PTR(value);
	v->fref = 0;
	v->len = stc_vu8->len;
	memcpy(v->elems, stc_vu8->elems, stc_vu8->len);
	return (NB_BYTEVECTOR << 48)|value;
}

scm_value
init_vector(const STATIC_Vector *stc_vec)
{
	Vector *v;
	scm_value value = scm_heap_alloc(sizeof(*v) + stc_vec->len * sizeof(scm_value));
	v = GET_PTR(value);
	v->fref = 0;
	v->len = stc_vec->len;
	for (size_t i = 0; i < stc_vec->len; ++i) {
		v->elems[i] = init_constant(stc_vec->elems[i]);
	}
	return (NB_VECTOR << 48)|value;
}

scm_value
init_list(const STATIC_List *stc_lst)
{
	size_t i = stc_lst->len;
	assert(i >= 2);
	i--;
	scm_value lst = init_constant(stc_lst->elems[i]);
	do {
		i--;
		lst = _cons(init_constant(stc_lst->elems[i]), lst);
	} while (i);
	return lst;
}

scm_value
make_function(klabel_t fn)
{
	return (NB_FUNCTION << 48)|((scm_value)fn);
}

scm_value
make_closure(void)
{
	Closure *clos;
	scm_value value =
		scm_heap_alloc(sizeof(*clos) + (sizeof(scm_value) * arg_stack.top));
	clos = GET_PTR(value);
	clos->fref = 0;
	clos->code = (klabel_t)pop_arg();
	clos->nfree_vars = arg_stack.top;
	for (size_t i = 0; arg_stack.top; ++i) {
		clos->free_vars[i] = pop_arg();
	}
	return (NB_CLOSURE << 48)|value;
}

klabel_t
procedure_fn(scm_value value)
{
	if (CLOSURE_P(value)) {
		Closure *c = GET_PTR(value);
		return c->code;
	} else if (FUNCTION_P(value)) {
		return GET_FN_PTR(value);
	} else {
		scm_assert(0, "type error expected <procedure");
	}
	return NULL;
}

klabel_t
function_fn(scm_value value)
{
	scm_assert(FUNCTION_P(value), "type error expected <procedure>");
	return GET_FN_PTR(value);
}

klabel_t
closure_fn(scm_value value)
{
	scm_assert(CLOSURE_P(value), "type error expected <procedure>");
	Closure *c = GET_PTR(value);
	return c->code;
}

scm_value
closure_ref(scm_value clos, i32 idx)
{
	Closure *c = GET_PTR(clos);
	return c->free_vars[idx];
}

scm_value
make_box(void)
{
	Box *box;
	scm_value value = scm_heap_alloc(sizeof(*box));
	box = GET_PTR(value);
	box->fref = 0;
	box->value = SCM_VOID;
	return (NB_BOX << 48)|value;
}

void
box_set(scm_value b, scm_value value)
{
	scm_assert(BOX_P(b), "type error expected variable");
	Box *box = GET_PTR(b);
	box->value = value;
}

scm_value
box_ref(scm_value b)
{
	scm_assert(BOX_P(b), "type error expected variable");
	Box *box = GET_PTR(b);
	return box->value;
}

scm_value
make_int(i64 x)
{
	scm_assert(x <= INT32_MAX, "bigint unimplemented");
	scm_assert(x >= INT32_MIN, "bigint unimplemented");
	scm_value value = x;
	return (NB_INT << 48)|value;
}

scm_value
make_char(i64 x)
{
	scm_assert(x <= INT32_MAX, "bigint unimplemented");
	scm_assert(x >= INT32_MIN, "bigint unimplemented");
	scm_value value = x;
	return (NB_CHAR << 48)|value;
}

scm_value
make_float(f64 x)
{
	union f2u n = {.f = x};
	return n.u;
}

f64
get_float(scm_value x)
{
	union f2u n = {.u = x};
	return n.f;
}

void
push_arg(scm_value x)
{
	if (arg_stack.top < ARG_STACK_LEN) {
		arg_stack.stk[arg_stack.top++] = x;
	} else {
		scm_assert(0, "ARG STACK OVERFLOW");
	}
}

void
push_ref(scm_value x)
{
	if (BOX_P(x)) {
		x = box_ref(x);
	}
	push_arg(x);
}

scm_value
pop_arg(void)
{
	if (arg_stack.top > 0) {
		arg_stack.top--;
		return arg_stack.stk[arg_stack.top];
	} else {
		scm_assert(0, "ARG STACK UNDERFLOW");
	}
	return SCM_VOID;
}

void
print_stk(void)
{
	for (size_t i = 0; i < arg_stack.top; ++i) {
		printf("[%zu] 0x%lx\n", i, arg_stack.stk[i]);
	}
}

scm_value
primop_cons(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value car = pop_arg();
	scm_value cdr = pop_arg();
	return _cons(car, cdr);
}

scm_value
prim_cons(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_cons());
	TAIL_CALL(k);
}

scm_value
primop_car(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value p = pop_arg();
	scm_assert(PAIR_P(p), "type error expected <pair>");
	Pair *pair = GET_PTR(p);
	return pair->car;
}

scm_value
prim_car(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_car());
	TAIL_CALL(k);
}

scm_value
primop_cdr(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value p = pop_arg();
	scm_assert(PAIR_P(p), "type error expected <pair>");
	Pair *pair = GET_PTR(p);
	return pair->cdr;
}

scm_value
prim_cdr(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_cdr());
	TAIL_CALL(k);
}

scm_value
primop_set_car(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value pair = pop_arg();
	scm_value val = pop_arg();
	scm_assert(PAIR_P(pair), "type error expected <pair>");
	Pair *p = GET_PTR(pair);
	p->car = val;
	return SCM_VOID;
}

scm_value
prim_set_car(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_set_car());
	TAIL_CALL(k);
}

scm_value
primop_set_cdr(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value pair = pop_arg();
	scm_value val = pop_arg();
	scm_assert(PAIR_P(pair), "type error expected <pair>");
	Pair *p = GET_PTR(pair);
	p->cdr = val;
	return SCM_VOID;
}

scm_value
prim_set_cdr(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_set_cdr());
	TAIL_CALL(k);
}

scm_value
primop_vector(void)
{
	Vector *vec;
	scm_value value =
		scm_heap_alloc(sizeof(*vec) + (sizeof(scm_value) * arg_stack.top));
	vec = GET_PTR(value);
	vec->fref = 0;
	vec->len = arg_stack.top;
	for (size_t i = 0; arg_stack.top; ++i) {
		vec->elems[i] = pop_arg();
	}
	return (NB_VECTOR << 48)|value;
}

scm_value
prim_vector(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_vector());
	TAIL_CALL(k);
}

scm_value
prim_void(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(SCM_VOID);
	TAIL_CALL(k);
}

scm_value
prim_boolean_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(BOOLEAN_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_char_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(CHAR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_null_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(NULL_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_pair_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(PAIR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_procedure_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(PROCEDURE_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_symbol_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(SYMBOL_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_bytevector_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(BYTEVECTOR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_number_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(NUMBER_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_string_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(STRING_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_vector_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(VECTOR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_record_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(RECORD_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
primop_eq(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value obj1 = pop_arg();
	scm_value obj2 = pop_arg();
	if (SYMBOL_P(obj1)
		&& SYMBOL_P(obj2)) {
		Symbol *s1 = GET_PTR(obj1);
		Symbol *s2 = GET_PTR(obj2);
		return ITOBOOL(s1->hash == s2->hash);
	}
	return ITOBOOL(obj1 == obj2);
}

scm_value
prim_eq(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_eq());
	TAIL_CALL(k);
}

static inline int num_eqxx(scm_value x, scm_value y);

scm_value
primop_eqv(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value x = pop_arg();
	scm_value y = pop_arg();
	if (NUMBER_P(x) && NUMBER_P(y)) {
		return ITOBOOL(num_eqxx(x, y));
	}
	push_arg(y);
	push_arg(x);
	return primop_eq();
}

scm_value
prim_eqv(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_eqv());
	TAIL_CALL(k);
}

scm_value
primop_equal(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value x = pop_arg();
	scm_value y = pop_arg();
	if (NUMBER_P(x) && NUMBER_P(y)) {
		return ITOBOOL(num_eqxx(x, y));
	}
	if (STRING_P(x) && STRING_P(y)) {
		push_arg(y);
		push_arg(x);
		return primop_string_eq();
	}
	if (VECTOR_P(x) && VECTOR_P(y)) {
		Vector *v1 = GET_PTR(x);
		Vector *v2 = GET_PTR(y);
		if (v1->len == v2->len) {
			for (size_t i = 0; i < v1->len; ++i) {
				push_arg(v2->elems[i]);
				push_arg(v1->elems[i]);
				if (primop_equal() == SCM_FALSE) {
					return SCM_FALSE;
				}
			}
			return SCM_TRUE;
		} else {
			return SCM_FALSE;
		}
	}
	if (BYTEVECTOR_P(x) && BYTEVECTOR_P(y)) {
		Bytevector *b1 = GET_PTR(x);
		Bytevector *b2 = GET_PTR(x);
		return ITOBOOL(b1->len == b2->len
					   && (memcmp(b1->elems, b2->elems, b1->len) == 0));
	}
	push_arg(y);
	push_arg(x);
	return primop_eqv();
}

scm_value
prim_equal(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_equal());
	TAIL_CALL(k);
}

static inline scm_value
addfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_float(x + (f64)GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float(x + get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
addix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		i64 n = x + GET_INTEGRAL(y);
		scm_assert(n <= INT32_MAX, "Integer overflow");
		scm_assert(n >= INT32_MIN, "Integer underflow");
		return make_int(n);
	} else if (FLOAT_P(y)) {
		return make_float((f64)x + get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
addxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return addix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return addfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
subfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_float(x - (f64)GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float(x - get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
subix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		i64 n = x - GET_INTEGRAL(y);
		scm_assert(n <= INT32_MAX, "Integer overflow");
		scm_assert(n >= INT32_MIN, "Integer underflow");
		return make_int(n);
	} else if (FLOAT_P(y)) {
		return make_float((f64)x - get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
subxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return subix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return subfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
mulfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_float(x * (f64)GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float(x * get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
mulix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		i64 n = x * GET_INTEGRAL(y);
		scm_assert(n <= INT32_MAX, "Integer overflow");
		scm_assert(n >= INT32_MIN, "Integer underflow");
		return make_int(n);
	} else if (FLOAT_P(y)) {
		return make_float((f64)x * get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
mulxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return mulix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return mulfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
divfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_float(x / (f64)GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float(x / get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
divix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_float((f64)x / (f64)GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float((f64)x / get_float(y));
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline scm_value
divxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return divix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return divfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return SCM_VOID;
}

static inline int
lessfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x < (f64)GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return x < get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
lessix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x < GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return (f64)x < get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
lessxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return lessix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return lessfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
grfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x > (f64)GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return x > get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
grix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x > GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return (f64)x > get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
grxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return grix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return grfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
leqfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x <= (f64)GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return x <= get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
leqix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x <= GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return (f64)x <= get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
leqxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return leqix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return leqfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
geqfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x >= (f64)GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return x >= get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
geqix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x >= GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return (f64)x >= get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
geqxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return geqix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return geqfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
num_eqfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x == (f64)GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return x == get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
num_eqix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x == GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return (f64)x == get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

static inline int
num_eqxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return num_eqix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return num_eqfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
	return 0;
}

scm_value
primop_add(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	scm_value sum;
	switch (arg_stack.top) {
	case 0: {
		sum = make_int(0);
	} break;
	case 1: {
		sum = make_int(pop_arg());
	} break;
	case 2: {
		scm_value x = pop_arg();
		scm_value y = pop_arg();
		sum = addxx(x, y);
	} break;
	default: {
		sum = pop_arg();
		while (arg_stack.top) {
			sum = addxx(sum, pop_arg());
		}
	} break;
	}
	return sum;
}

scm_value
prim_add(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_add());
	TAIL_CALL(k);
}

scm_value
primop_sub(void)
{
	scm_assert(chk_args(1, 1), "arity error");
	scm_value total;
	switch (arg_stack.top) {
	case 1: {
		total = subxx(make_int(0), pop_arg());
	} break;
	case 2: {
		scm_value x = pop_arg();
		scm_value y = pop_arg();
		total = subxx(x, y);
	} break;
	default: {
		scm_value fst = pop_arg();
		total = primop_add();
		total = subxx(fst, total);
	} break;
	}
	return total;
}

scm_value
prim_sub(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_sub());
	TAIL_CALL(k);
}

scm_value
prim_inc(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value k = pop_arg();
	scm_value x = pop_arg();
	push_arg(addxx(x, make_int(1)));
	TAIL_CALL(k);
}

scm_value
prim_dec(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value k = pop_arg();
	scm_value x = pop_arg();
	push_arg(subxx(x, make_int(1)));
	TAIL_CALL(k);
}

scm_value
primop_mul(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	scm_value total;
	switch (arg_stack.top) {
	case 0: {
		total = make_int(1);
	} break;
	case 1: {
		total = make_int(pop_arg());
	} break;
	case 2: {
		scm_value x = pop_arg();
		scm_value y = pop_arg();
		total = mulxx(x, y);
	} break;
	default: {
		total = pop_arg();
		while (arg_stack.top) {
			total = mulxx(total, pop_arg());
		}
	} break;
	}
	return total;
}

scm_value
prim_mul(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_mul());
	TAIL_CALL(k);
}

scm_value
primop_div(void)
{
	scm_assert(chk_args(1, 1), "arity error");
	scm_value total;
	switch (arg_stack.top) {
	case 1: {
		total = divxx(make_int(1), pop_arg());
	} break;
	case 2: {
		scm_value x = pop_arg();
		scm_value y = pop_arg();
		total = divxx(x, y);
	} break;
	default: {
		scm_value fst = pop_arg();
		total = primop_mul();
		total = divxx(fst, total);
	} break;
	}
	return total;
}

scm_value
prim_div(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_div());
	TAIL_CALL(k);
}

scm_value
primop_num_eq(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	if (arg_stack.top < 2) {
		while (arg_stack.top) {
			scm_assert(NUMBER_P(pop_arg()), "type error expected <number>");
		}
		return SCM_TRUE;
	}
	scm_value x, y;
	x = pop_arg();
	while (arg_stack.top) {
		y = pop_arg();
		if (!num_eqxx(x, y)) {
			return SCM_FALSE;
		}
		x = y;
	}
	return SCM_TRUE;
}

scm_value
prim_num_eq(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_num_eq());
	TAIL_CALL(k);
}

scm_value
primop_less(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	if (arg_stack.top < 2) {
		while (arg_stack.top) {
			scm_assert(NUMBER_P(pop_arg()), "type error expected <number>");
		}
		return SCM_TRUE;
	}
	scm_value x, y;
	x = pop_arg();
	while (arg_stack.top) {
		y = pop_arg();
		if (!lessxx(x, y)) {
			return SCM_FALSE;
		}
		x = y;
	}
	return SCM_TRUE;
}

scm_value
prim_less(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_less());
	TAIL_CALL(k);
}

scm_value
primop_gr(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	if (arg_stack.top < 2) {
		while (arg_stack.top) {
			scm_assert(NUMBER_P(pop_arg()), "type error expected <number>");
		}
		return SCM_TRUE;
	}
	scm_value x, y;
	x = pop_arg();
	while (arg_stack.top) {
		y = pop_arg();
		if (!grxx(x, y)) {
			return SCM_FALSE;
		}
		x = y;
	}
	return SCM_TRUE;
}

scm_value
prim_gr(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_gr());
	TAIL_CALL(k);
}

scm_value
primop_leq(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	if (arg_stack.top < 2) {
		while (arg_stack.top) {
			scm_assert(NUMBER_P(pop_arg()), "type error expected <number>");
		}
		return SCM_TRUE;
	}
	scm_value x, y;
	x = pop_arg();
	while (arg_stack.top) {
		y = pop_arg();
		if (!leqxx(x, y)) {
			return SCM_FALSE;
		}
		x = y;
	}
	return SCM_TRUE;
}

scm_value
prim_leq(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_leq());
	TAIL_CALL(k);
}

scm_value
primop_geq(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	if (arg_stack.top < 2) {
		while (arg_stack.top) {
			scm_assert(NUMBER_P(pop_arg()), "type error expected <number>");
		}
		return SCM_TRUE;
	}
	scm_value x, y;
	x = pop_arg();
	while (arg_stack.top) {
		y = pop_arg();
		if (!geqxx(x, y)) {
			return SCM_FALSE;
		}
		x = y;
	}
	return SCM_TRUE;
}

scm_value
prim_geq(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_geq());
	TAIL_CALL(k);
}

scm_value
primop_zero_p(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value x = pop_arg();
	return num_eqxx(x, make_int(0));
}

scm_value
prim_zero_p(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_zero_p());
	TAIL_CALL(k);
}

scm_value
primop_positive_p(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value x = pop_arg();
	return grxx(x, make_int(0));
}

scm_value
prim_positive_p(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_positive_p());
	TAIL_CALL(k);
}

scm_value
primop_negative_p(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value x = pop_arg();
	return lessxx(x, make_int(0));
}

scm_value
prim_negative_p(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_negative_p());
	TAIL_CALL(k);
}

scm_value
primop_string_len(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value value = pop_arg();
	scm_assert(STRING_P(value), "type error, expected <string>");
	String *s = GET_PTR(value);
	return make_int(s->len);
}

scm_value
prim_string_len(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_len());
	TAIL_CALL(k);
}

scm_value
primop_string_ref(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value str = pop_arg();
	scm_value idx = pop_arg();
	scm_assert(STRING_P(str), "type error, expected <string>");
	scm_assert(INTEGER_P(idx), "type error, expected <integer>");
	String *s = GET_PTR(str);
	i64 i = GET_INTEGRAL(idx);
	scm_assert(i >= 0, "value error, index expected to be non-negative integer");
	scm_assert(i < s->len, "value error, index out of bounds");
	return make_char(s->elems[i]);
}

scm_value
prim_string_ref(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_ref());
	TAIL_CALL(k);
}

static inline int
string_eq(scm_value v1, scm_value v2)
{
	scm_assert(STRING_P(v1), "type error, expected <string>");
	scm_assert(STRING_P(v2), "type error, expected <string>");
	String *s1 = GET_PTR(v1);
	String *s2 = GET_PTR(v2);
	return s1->len == s2->len && (memcmp(s1->elems, s2->elems, s1->len) == 0);
}

scm_value
primop_string_eq(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	if (arg_stack.top < 2) {
		while (arg_stack.top) {
			scm_assert(STRING_P(pop_arg()), "type error expected <number>");
		}
		return SCM_TRUE;
	}
	scm_value fst = pop_arg();
	while (arg_stack.top) {
		if (!string_eq(fst, pop_arg())) {
			return SCM_FALSE;
		}
	}
	return SCM_TRUE;
}

scm_value
prim_string_eq(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_eq());
	TAIL_CALL(k);
}

static inline void
scm_print(scm_value value, int quote_p)
{
	if (VOID_P(value)) {
		printf("#<void>");
	} else if (INTEGER_P(value)) {
		printf("%d", GET_INTEGRAL(value));
	} else if (FLOAT_P(value)) {
		printf("%g", get_float(value));
	} else if (CHAR_P(value)) {
		int c = GET_INTEGRAL(value);
		if (quote_p) {
			switch (c) {
			case 0x0: {
				printf("#\\nul");
			} break;
			case 0x7: {
				printf("#\\alarm");
			} break;
			case 0x8: {
				printf("#\\backspace");
			} break;
			case 0x9: {
				printf("#\\tab");
			} break;
			case 0xa: {
				printf("#\\newline");
			} break;
			case 0xb: {
				printf("#\\vtab");
			} break;
			case 0xc: {
				printf("#\\page");
			} break;
			case 0xd: {
				printf("#\\return");
			} break;
			case 0x1b: {
				printf("#\\esc");
			} break;
			case 0x20: {
				printf("#\\space");
			} break;
			case 0x7f: {
				printf("#\\delete");
			} break;
			default: {
				printf("#\\%c", c);
			} break;
			}
		} else {
			putchar(c);
		}
	} else if (value == SCM_TRUE) {
		printf("#t");
	} else if (value == SCM_FALSE) {
		printf("#f");
	} else if (STRING_P(value)) {
		String *s = GET_PTR(value);
		if (quote_p) {
			printf("\"%.*s\"", (int)s->len, s->elems);
		} else {
			printf("%.*s", (int)s->len, s->elems);
		}
	} else if (SYMBOL_P(value)) {
		Symbol *s = GET_PTR(value);
		printf("%.*s", s->len, s->name);
	} else if (PAIR_P(value)) {
		Pair *p = GET_PTR(value);
		printf("(");
		for (;;) {
			if (PAIR_P(p->cdr)) {
				scm_print(p->car, quote_p);
				printf(" ");
				p = GET_PTR(p->cdr);
			} else if (NULL_P(p->cdr)) {
				scm_print(p->car, quote_p);
				printf(")");
				break;
			} else {
				scm_print(p->car, quote_p);
				printf(" . ");
				scm_print(p->cdr, quote_p);
				printf(")");
				break;
			}
		}
	} else if (PROCEDURE_P(value)) {
		printf("#<procedure @ #x%lx>", (u64)closure_fn(value));
	} else if (VECTOR_P(value)) {
		Vector *vec = GET_PTR(value);
		printf("#(");
		if (vec->len) {
			size_t i;
			for (i = 0; i < vec->len - 1; ++i) {
				scm_print(vec->elems[i], quote_p);
				printf(" ");
			}
			scm_print(vec->elems[i], quote_p);
		}
		printf(")");
	} else if (BYTEVECTOR_P(value)) {
		Bytevector *vec = GET_PTR(value);
		printf("#vu8(");
		if (vec->len) {
			size_t i;
			for (i = 0; i < vec->len - 1; ++i) {
				printf("%d ", vec->elems[i]);
			}
			printf("%d)", vec->elems[i]);
		} else {
			printf(")");
		}
	} else {
		printf("0x%lx\n", value);
		scm_assert(0, "unimplemented");
	}
}

scm_value
primop_write(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value value = pop_arg();
	scm_print(value, 1);
	return SCM_VOID;
}

scm_value
prim_write(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_write());
	TAIL_CALL(k);
}

scm_value
primop_display(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value value = pop_arg();
	scm_print(value, 0);
	return SCM_VOID;
}

scm_value
prim_display(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_display());
	TAIL_CALL(k);
}

scm_value
primop_newline(void)
{
	scm_assert(chk_args(0, 0), "arity error");
	putchar('\n');
	return SCM_VOID;
}

scm_value
prim_newline(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_newline());
	TAIL_CALL(k);
}

static scm_value
tl_exit(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value x = pop_arg();
	if (!VOID_P(x)) {
		push_arg(x);
		primop_write();
		primop_newline();
	}
	longjmp(exit_point, 1);
}

static scm_value
default_excption_handler(UNUSED_ATTR scm_value self)
{
	exit(1);
	return SCM_VOID;
}

scm_value
scm_module_lookup(char *name, scm_value module)
{
	scm_assert(VECTOR_P(module), "type error expected <vector>");
	Vector *vec = GET_PTR(module);
	for (size_t i = 0; i < vec->len; ++i) {
		Pair *entry = GET_PTR(vec->elems[i]);
		if (strcmp(name, (char *)entry->car) == 0) {
			return entry->cdr;
		}
	}
	scm_assert(0, "error undefined identifier");
	return SCM_VOID;
}

scm_value
scm_runtime_load_dynamic(void)
{
	push_arg(module_entry("cons", prim_cons));
	push_arg(module_entry("car", prim_car));
	push_arg(module_entry("cdr", prim_cdr));
	push_arg(module_entry("vector", prim_vector));
	push_arg(module_entry("void", prim_void));
	push_arg(module_entry("boolean?", prim_boolean_p));
	push_arg(module_entry("char?", prim_char_p));
	push_arg(module_entry("null?", prim_null_p));
	push_arg(module_entry("pair?", prim_pair_p));
	push_arg(module_entry("procedure?", prim_procedure_p));
	push_arg(module_entry("symbol?", prim_symbol_p));
	push_arg(module_entry("bytevector?", prim_bytevector_p));
	push_arg(module_entry("number?", prim_number_p));
	push_arg(module_entry("string?", prim_string_p));
	push_arg(module_entry("vector?", prim_vector_p));
	push_arg(module_entry("eq?", prim_eq));
	push_arg(module_entry("eqv?", prim_eqv));
	push_arg(module_entry("equal?", prim_equal));
	/* arithmetic */
	push_arg(module_entry("+", prim_add));
	push_arg(module_entry("-", prim_sub));
	push_arg(module_entry("*", prim_mul));
	push_arg(module_entry("/", prim_div));
	push_arg(module_entry("=", prim_num_eq));
	push_arg(module_entry("<", prim_less));
	push_arg(module_entry(">", prim_gr));
	push_arg(module_entry("<=", prim_leq));
	push_arg(module_entry(">=", prim_geq));
	push_arg(module_entry("zero?", prim_zero_p));
	push_arg(module_entry("positive?", prim_positive_p));
	push_arg(module_entry("negative?", prim_negative_p));
	/* string */
	push_arg(module_entry("string-length", prim_string_len));
	push_arg(module_entry("string-ref", prim_string_ref));
	push_arg(module_entry("string=?", prim_string_eq));
	/* IO */
	push_arg(module_entry("write", prim_write));
	push_arg(module_entry("display", prim_display));
	push_arg(module_entry("newline", prim_newline));
	return primop_vector();
}

int
trampoline(scm_value cc)
{
	scm_value module = scm_runtime_load_dynamic();
	push_arg((scm_value)tl_exit);
	scm_value kexit = make_closure();
	push_arg((scm_value)default_excption_handler);
	exception_handler = make_closure();
	push_arg(module);
	push_arg(kexit);
	int ext = setjmp(exit_point);
	while (ext == 0) {
		cc = procedure_fn(cc)(cc);
	}
	printf("\n====================================\n");
	printf("DEBUG:\n");
	printf("bytes-allocated = %zu\n", bytes_allocated);
	printf("bytes-used = %zu\n", heap_working->idx);
	printf("gc-cycles = %zu\n", gc_cycles);
	return ext;
}
