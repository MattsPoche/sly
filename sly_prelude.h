#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "common_def.h"

/* NAN Boxing */
#define NAN_BITS 0x7ff0LU
#define NB_VOID			(NAN_BITS|tt_void) // bool
#define NB_BOOL			(NAN_BITS|tt_bool) // bool
#define NB_CHAR			(NAN_BITS|tt_char) // char
#define NB_INT			(NAN_BITS|tt_int) // int
#define NB_PAIR			(NAN_BITS|tt_pair) // pair
#define NB_SYMBOL		(NAN_BITS|tt_symbol) // symbol
#define NB_BYTEVECTOR	(NAN_BITS|tt_bytevector) // byte-vector
#define NB_STRING		(NAN_BITS|tt_string) // string
#define NB_VECTOR		(NAN_BITS|tt_vector) // vector
#define NB_RECORD		(NAN_BITS|tt_record) // record
#define NB_BOX		    (NAN_BITS|tt_box) // referance
#define NB_CLOSURE	    (NAN_BITS|tt_closure) // closure
#define SCM_NULL  (NB_PAIR << 48)
#define SCM_FALSE (NB_BOOL << 48)
#define SCM_TRUE  (SCM_FALSE + 1)
#define SCM_VOID  (NB_VOID << 48)
#define TAG_VALUE(type, val_bits) (((type) << 48)|(val_bits))
#define TYPEOF(value) (value >> 48)
#define GET_PTR(value) \
	((void *)(((value) & ((1LU << 32) - 1)) + heap_working->start))
#define GET_INTEGRAL(value)  ((i32)(value & ((1LU << 32) - 1)))
#define NULL_P(value) (value == SCM_NULL)
#define BOOLEAN_P(value) (TYPEOF(value) == NB_BOOL)
#define INTEGER_P(value) (TYPEOF(value) == NB_INT)
#define FLOAT_P(value) (((value >> 52) & 0x7ff) ^ 0x7ff)
#define NUMBER_P(value) (INTEGER_P(value) || FLOAT_P(value))
#define CHAR_P(value) (TYPEOF(value) == NB_CHAR)
#define PAIR_P(value) (TYPEOF(value) == NB_PAIR && !NULL_P(value))
#define SYMBOL_P(value) (TYPEOF(value) == NB_SYMBOL)
#define BYTEVECTOR_P(value) (TYPEOF(value) == NB_BYTEVECTOR)
#define STRING_P(value) (TYPEOF(value) == NB_STRING)
#define VECTOR_P(value) (TYPEOF(value) == NB_VECTOR)
#define RECORD_P(value) (TYPEOF(value) == NB_RECORD)
#define BOX_P(value) (TYPEOF(value) == NB_BOX)
#define CLOSURE_P(value) (TYPEOF(value) == NB_CLOSURE)
#define PROCEDURE_P(value) CLOSURE_P(value)
#define TAIL_CALL(proc) return _tail_call(proc)
#define ITOBOOL(i) (i ? SCM_TRUE : SCM_FALSE)
#define BOOLTOI(b) (b == SCM_FALSE ? 0 : 1)

enum type_tag {
	tt_inf = 0,
	tt_void,
	tt_bool,
	tt_char,
	tt_int,
	tt_pair,
	tt_symbol,
	tt_bytevector,
	tt_string,
	tt_vector,
	tt_record,
	tt_box,
	tt_closure,
	TT_COUNT,
	tt_float,
};

static_assert(TT_COUNT <= 0xf);

typedef u64 scm_value;
typedef struct _closure Closure;
typedef scm_value (*klabel_t)(scm_value self);

typedef struct _closure {
	u32 fref;
	u32 nfree_vars;
	klabel_t code;
	scm_value free_vars[];
} Closure;

typedef struct _pair {
	u32 fref;
	scm_value car;
	scm_value cdr;
} Pair;

typedef struct _symbol {
	u32 fref;
	u32 len;
	u64 hash;
	u8 name[];
} Symbol;

typedef struct _string {
	u32 fref;
	u32 len;
	u8 elems[];
} String;

typedef struct _bytevector {
	u32 fref;
	u32 len;
	u8 elems[];
} Bytevector;

typedef struct _vector {
	u32 fref;
	u32 len;
	scm_value elems[];
} Vector;

typedef struct _record {
	u32 fref;
	u32 len;
	scm_value elems[];
} Record;

typedef struct _box {
	u32 fref;
	scm_value value;
} Box;

struct constant {
	enum type_tag tt;
	union {
		u64 as_uint;
		i64 as_int;
		f64 as_float;
		void *as_ptr;
	} u;
};

typedef struct _static_string {
	u32 len;
	u8 elems[];
} STATIC_String;

typedef struct _static_symbol {
	u64 hash;
	u32 len;
	u8 elems[];
} STATIC_Symbol;

typedef struct _static_bytevector {
	u32 len;
	u8 elems[];
} STATIC_Bytevector;

typedef struct _static_vector {
	u32 len;
	struct constant elems[];
} STATIC_Vector;

typedef struct _static_list {
	u32 len;
	struct constant elems[];
} STATIC_List;

typedef struct _mem_pool {
	size_t sz;
	size_t idx;
	u8 *start;
} Mem_Pool;

union f2u {f64 f; u64 u;};

int trampoline(scm_value cc);
static inline scm_value _tail_call(scm_value proc);
static inline void scm_heap_init(void);
static inline size_t mem_align_offset(size_t addr);
static inline size_t scm_heap_alloc(size_t sz);
static inline void _scm_assert(int p, char *msg, const char *func_name);
static inline scm_value cons_rest(void);
static inline scm_value _cons(scm_value car, scm_value cdr);
static inline scm_value init_constant(struct constant cnst);
static inline scm_value make_constant(size_t idx);
static inline scm_value load_interned_constant(size_t idx);
static inline void scm_intern_constants(size_t len);
static inline scm_value init_string(const STATIC_String *stc_str);
static inline scm_value init_symbol(const STATIC_Symbol *stc_sym);
static inline scm_value init_bytevector(const STATIC_Bytevector *stc_vu8);
static inline scm_value init_vector(const STATIC_Vector *stc_vec);
static inline scm_value init_list(const STATIC_List *stc_lst);
static inline scm_value make_closure(void);
static inline klabel_t closure_fn(scm_value value);
static inline scm_value closure_ref(scm_value clos, i32 idx);
static inline void push_arg(scm_value x);
static inline void push_ref(scm_value x);
static inline scm_value pop_arg(void);
static inline scm_value make_box(void);
static inline void box_set(scm_value b, scm_value value);
static inline scm_value box_ref(scm_value b);
static inline scm_value make_int(i64 x);
static inline scm_value make_char(i64 x);
static inline scm_value make_float(f64 x);
static inline f64 get_float(scm_value x);
static inline int chk_args(size_t req, int rest);
static inline scm_value primop_cons(void);
static inline scm_value prim_cons(scm_value self);
static inline scm_value primop_car(void);
static inline scm_value prim_car(scm_value self);
static inline scm_value primop_cdr(void);
static inline scm_value prim_cdr(scm_value self);
/* type predicates */
static inline scm_value prim_boolean_p(scm_value self);
static inline scm_value prim_char_p(scm_value self);
static inline scm_value prim_null_p(scm_value self);
static inline scm_value prim_pair_p(scm_value self);
static inline scm_value prim_procedure_p(scm_value self);
static inline scm_value prim_symbol_p(scm_value self);
static inline scm_value prim_bytevector_p(scm_value self);
static inline scm_value prim_number_p(scm_value self);
static inline scm_value prim_string_p(scm_value self);
static inline scm_value prim_vector_p(scm_value self);
#if 0
// unimplemented
static inline scm_value prim_eof_object_p(scm_value self);
static inline scm_value prim_port_p(scm_value self);
#endif
/* Equivalence predicates */
static inline scm_value primop_eq(void);
static inline scm_value prim_eq(scm_value self);
static inline scm_value primop_eqv(void);
static inline scm_value prim_eqv(scm_value self);

/* arithmetic procedures */
static inline scm_value primop_add(void);
static inline scm_value prim_add(scm_value self);
static inline scm_value primop_sub(void);
static inline scm_value prim_sub(scm_value self);
static inline scm_value primop_mul(void);
static inline scm_value prim_mul(scm_value self);
static inline scm_value primop_div(void);
static inline scm_value prim_div(scm_value self);
static inline scm_value primop_num_eq(void);
static inline scm_value prim_num_eq(scm_value self);
static inline scm_value primop_less(void);
static inline scm_value prim_less(scm_value self);
//////////////////////////////////////////////////////////
static inline scm_value primop_string_len(void);
static inline scm_value prim_string_len(scm_value self);
static inline scm_value primop_string_ref(void);
static inline scm_value prim_string_ref(scm_value self);
static inline scm_value primop_string_eq(void);
static inline scm_value prim_string_eq(scm_value self);
/* IO procedures */
static inline scm_value primop_display(void);
static inline scm_value prim_display(scm_value self);
static inline scm_value primop_newline(void);
static inline scm_value prim_newline(scm_value self);
static inline void print_stk(void);

#define scm_assert(p, msg) _scm_assert(p, msg, __func__)
#define ARG_STACK_LEN 0xffLU
static struct {
	size_t top;
	scm_value stk[ARG_STACK_LEN];
} arg_stack;

#define HEAP_PAGE_SZ (1LU << 12)
#define HEAP_USED() (heap.idx)
#define HEAP_FREE() (heap.sz - HEAP_USED())
static Mem_Pool mp0 = {0};
static Mem_Pool mp1 = {0};
static Mem_Pool *heap_working = &mp0;
static Mem_Pool *heap_free = &mp1;
static size_t prev_saved = 0;
static jmp_buf exit_point;
static scm_value exception_handler;
static struct constant constants[];
static scm_value *interned;

scm_value
_tail_call(scm_value proc)
{
	if (BOX_P(proc)) {
		proc = box_ref(proc);
	}
	scm_assert(PROCEDURE_P(proc), "type error, expected procedure");
	if (arg_stack.top == 0) {
		push_arg(SCM_VOID);
	}
	return proc;
}

void
scm_heap_init(void)
{
	mp0.sz = HEAP_PAGE_SZ;
	mp0.start = malloc(HEAP_PAGE_SZ);
	scm_assert(mp0.start != NULL, "OS is too greedy :(");
	mp0.idx += sizeof(scm_value);
}

size_t
mem_align_offset(size_t addr)
{
	return ((~addr) + 1) & 0x7;
}

size_t
scm_heap_alloc(size_t sz)
{
	if (sz >= (heap_working->sz - heap_working->idx)) {
		heap_working->sz *= 2;
		heap_working->start = realloc(heap_working->start,
									  heap_working->sz);
	}
	size_t i = heap_working->idx;
	heap_working->idx += sz;
	heap_working->idx += mem_align_offset(heap_working->idx);
	return i;
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
	case tt_record: {
		scm_assert(0, "unimplemented");
	} break;
	case tt_box: {
		v = make_constant(cnst.u.as_uint);
	} break;
	case tt_closure: {
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
	c = init_constant(constants[idx]);
	interned[idx] = c;
	return c;
}

scm_value
load_interned_constant(size_t idx)
{
	return interned[idx];
}

void
scm_intern_constants(size_t len)
{
	interned = calloc(len, sizeof(*interned));
	scm_assert(interned != NULL, "malloc failed");
	for (size_t i = 0; i < len; ++i) {
		interned[i] = make_constant(i);
	}
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
make_closure(void)
{
	scm_value value = scm_heap_alloc(sizeof(Closure)
									 + (sizeof(scm_value) * arg_stack.top));
	Closure *clos = GET_PTR(value);
	clos->fref = 0;
	clos->code = (klabel_t)pop_arg();
	clos->nfree_vars = arg_stack.top;
	for (size_t i = 0; arg_stack.top; ++i) {
		clos->free_vars[i] = pop_arg();
	}
	return (NB_CLOSURE << 48)|value;
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
prim_cons(scm_value self)
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
prim_car(scm_value self)
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
prim_cdr(scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_cdr());
	TAIL_CALL(k);
}

scm_value
prim_boolean_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(BOOLEAN_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_char_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(CHAR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_null_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(NULL_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_pair_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(PAIR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_procedure_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(PROCEDURE_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_symbol_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(SYMBOL_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_bytevector_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(BYTEVECTOR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_number_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(NUMBER_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_string_p(scm_value self)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(STRING_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_vector_p(scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	push_arg(ITOBOOL(VECTOR_P(pop_arg())));
	TAIL_CALL(k);
}

scm_value
prim_record_p(scm_value self)
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
	return ITOBOOL(obj1 == obj2);
}

scm_value
prim_eq(scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_eq());
	TAIL_CALL(k);
}

scm_value
primop_eqv(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	if (NUMBER_P(arg_stack.stk[1])
		&& NUMBER_P(arg_stack.stk[0])) {
		return primop_num_eq();
	}
	return primop_eq();
}

scm_value prim_eqv(scm_value self);


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
}

static inline int
eqfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x == (f64)GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return x == get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
}

static inline int
eqix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return x == GET_INTEGRAL(y);
	} else if (FLOAT_P(y)) {
		return (f64)x == get_float(y);
	} else {
		scm_assert(0, "Type error");
	}
}

static inline int
eqxx(scm_value x, scm_value y)
{
	if (INTEGER_P(x)) {
		return eqix(GET_INTEGRAL(x), y);
	} else if (FLOAT_P(x)) {
		return eqfx(get_float(x), y);
	} else {
		scm_assert(0, "Type error");
	}
}

scm_value
primop_add(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	scm_value sum = make_int(0);
	while (arg_stack.top) {
		sum = addxx(sum, pop_arg());
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
	if (arg_stack.top == 1) {
		return subxx(make_int(0), pop_arg());
	}
	scm_value fst = pop_arg();
	scm_value sum = make_int(0);
	while (arg_stack.top) {
		sum = addxx(sum, pop_arg());
	}
	return subxx(fst, sum);
}

scm_value
prim_sub(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_sub());
	TAIL_CALL(k);
}

scm_value
primop_mul(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	scm_value total = make_int(1);
	while (arg_stack.top) {
		total = mulxx(total, pop_arg());
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
	if (arg_stack.top == 1) {
		return divxx(make_float(1.0), pop_arg());
	}
	scm_value fst = pop_arg();
	scm_value total = make_int(1);
	while (arg_stack.top) {
		total = mulxx(total, pop_arg());
	}
	return divxx(fst, total);
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
		return SCM_TRUE;
	}
	scm_value x, y;
	x = pop_arg();
	while (arg_stack.top) {
		y = pop_arg();
		if (!eqxx(x, y)) {
			return SCM_FALSE;
		}
		x = y;
	}
	return SCM_TRUE;
}

scm_value
prim_num_eq(scm_value self)
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
prim_less(scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_less());
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
prim_string_len(scm_value self)
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
prim_string_ref(scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_ref());
	TAIL_CALL(k);
}

scm_value
primop_string_eq(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value v1 = pop_arg();
	scm_value v2 = pop_arg();
	scm_assert(STRING_P(v1), "type error, expected <string>");
	scm_assert(STRING_P(v2), "type error, expected <string>");
	String *s1 = GET_PTR(v1);
	String *s2 = GET_PTR(v2);
	if (s1->len == s2->len) {
		size_t len = s1->len;
		for (size_t i = 0; i < len; ++i) {
			if (s1->elems[i] != s2->elems[i]) {
				return SCM_FALSE;
			}
		}
		return SCM_TRUE;
	}
	return SCM_FALSE;
}

scm_value
prim_string_eq(scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_eq());
	TAIL_CALL(k);
}

scm_value
primop_display(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value value = pop_arg();
	if (INTEGER_P(value)) {
		printf("%d", GET_INTEGRAL(value));
	} else if (FLOAT_P(value)) {
		printf("%g", get_float(value));
	} else if (value == SCM_TRUE) {
		printf("#t");
	} else if (value == SCM_FALSE) {
		printf("#f");
	} else if (STRING_P(value)) {
		String *s = GET_PTR(value);
		printf("%.*s", (int)s->len, s->elems);
	} else if (SYMBOL_P(value)) {
		Symbol *s = GET_PTR(value);
		printf("%.*s", s->len, s->name);
	} else if (PAIR_P(value)) {
		Pair *p = GET_PTR(value);
		printf("(");
		for (;;) {
			if (PAIR_P(p->cdr)) {
				push_arg(p->car);
				primop_display();
				printf(" ");
				p = GET_PTR(p->cdr);
			} else if (NULL_P(p->cdr)) {
				push_arg(p->car);
				primop_display();
				printf(")");
				break;
			} else {
				push_arg(p->car);
				primop_display();
				printf(" . ");
				push_arg(p->cdr);
				primop_display();
				printf(")");
				break;
			}
		}
	} else if (PROCEDURE_P(value)) {
		printf("#<procedure @ #x%lx>", closure_fn(value));
	} else if (VECTOR_P(value)) {
		Vector *vec = GET_PTR(value);
		printf("#(");
		if (vec->len) {
			size_t i;
			for (i = 0; i < vec->len - 1; ++i) {
				push_arg(vec->elems[i]);
				primop_display();
				printf(" ");
			}
			push_arg(vec->elems[i]);
			primop_display();
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
	return SCM_VOID;
}

scm_value
prim_display(scm_value self)
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
prim_newline(scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_newline());
	TAIL_CALL(k);
}

static scm_value
tl_exit(UNUSED_ATTR scm_value self)
{
	primop_display();
	primop_newline();
	longjmp(exit_point, 1);
}

static scm_value
default_excption_handler(UNUSED_ATTR scm_value self)
{
	exit(1);
	return SCM_VOID;
}

int
trampoline(scm_value cc)
{
	push_arg((scm_value)tl_exit);
	scm_value kexit = make_closure();
	push_arg((scm_value)default_excption_handler);
	exception_handler = make_closure();
	push_arg(kexit);
	int ext = setjmp(exit_point);
	while (ext == 0) {
		cc = closure_fn(cc)(cc);
	}
	return ext;
}
