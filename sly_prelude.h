#include <assert.h>
#include <stdio.h>
#include "common_def.h"

/* NAN Boxing */
#define NAN_BITS 0x7ff0LU
#define NB_BOOL			(NAN_BITS|0x1) // bool
#define NB_CHAR			(NAN_BITS|0x2) // char
#define NB_INT			(NAN_BITS|0x3) // int
#define NB_PAIR			(NAN_BITS|0x4) // pair
#define NB_SYMBOL		(NAN_BITS|0x5) // symbol
#define NB_BYTE_VECTOR	(NAN_BITS|0x6) // byte-vector
#define NB_STRING		(NAN_BITS|0x7) // string
#define NB_VECTOR		(NAN_BITS|0x8) // vector
#define NB_RECORD		(NAN_BITS|0x9) // record
#define NB_BOX		    (NAN_BITS|0xa) // referance
#define NB_CLOSURE	    (NAN_BITS|0xb) // closure
#define SCM_NULL (NB_PAIR << 48)
#define SCM_FALSE (NB_BOOL << 48)
#define SCM_TRUE (SCM_FALSE + 1)
#define TAG_VALUE(type, val_bits) (((type) << 48)|(val_bits))
#define TYPEOF(value) (value >> 48)
#define GET_PTR(value) ((void *)(value & ((1LU << 48) - 1)))
#define GET_INTEGRAL(value)  (value & ((1LU << 32) - 1))
#define BOOL_P(value) (TYPEOF(value) == NB_BOOL)
#define INTEGER_P(value) (TYPEOF(value) == NB_INT)
#define FLOAT_P(value) (((value >> 52) & 0x7ff) ^ 0x7ff)
#define CHAR_P(value) (TYPEOF(value) == NB_CHAR)
#define PAIR_P(value) (TYPEOF(value) == NB_PAIR)
#define SYMBOL_P(value) (TYPEOF(value) == NB_SYMBOL)
#define BYTE_VECTOR_P(value) (TYPEOF(value) == NB_BYTE_VECTOR)
#define STRING_P(value) (TYPEOF(value) == NB_STRING)
#define VECTOR_P(value) (TYPEOF(value) == NB_VECTOR)
#define RECORD_P(value) (TYPEOF(value) == NB_RECORD)
#define BOX_P(value) (TYPEOF(value) == NB_BOX)
#define CLOSURE_P(value) (TYPEOF(value) == NB_CLOSURE)
#define PROCEDURE_P(value) CLOSURE_P(value)
#define TAIL_CALL(proc)							\
	do {										\
		assert(PROCEDURE_P(proc));				\
		return GET_PTR(proc);					\
	} while (0)

typedef u64 scm_value;
typedef struct _closure Closure;
typedef Closure *(*klabel_t)(Closure *self);

typedef struct _closure {
	klabel_t code;
	scm_value free_vars[];
} Closure;

typedef struct _pair {
	scm_value car;
	scm_value cdr;
} Pair;

typedef struct _symbol {
	u64 hash;
	size_t len;
	u8 name[];
} Symbol;

typedef struct _byte_vector {
	size_t len;
	u8 elems[];
} Byte_Vector;

typedef struct _vector {
	size_t len;
	scm_value elems[];
} Vector;

typedef struct _record {
	size_t len;
	scm_value elems[];
} Record;

union f2u {
	f64 f;
	u64 u;
};

static inline void trampoline(Closure *cc);
static inline void *alloc_cells(size_t n);
static inline scm_value cons_rest(size_t req);
static inline scm_value _cons(scm_value car, scm_value cdr);
static inline scm_value make_closure(void);
static inline void push_arg(scm_value x);
static inline scm_value pop_arg(void);
static inline scm_value make_int(i32 x);
static inline scm_value make_float(f64 x);
static inline f64 get_float(scm_value x);
static inline int chk_args(size_t req, int rest);
static inline scm_value primop_add(void);
static inline Closure *prim_add(Closure *self);
static inline scm_value primop_sub(void);
static inline Closure *prim_sub(Closure *self);
static inline scm_value primop_mul(void);
static inline Closure *prim_mul(Closure *self);
static inline scm_value primop_div(void);
static inline Closure *prim_div(Closure *self);
static inline scm_value primop_less(void);
static inline Closure *prim_less(Closure *self);
static inline void print_stk(void);

#define ARG_STACK_LEN 0xffLU
static struct {
	size_t top;
	scm_value stk[ARG_STACK_LEN];
} arg_stack;

static Closure *exception_handler;

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

void *
alloc_cells(size_t n)
{
	void *ptr = malloc(n * sizeof(scm_value));
	assert(ptr != NULL);
	return ptr;
}

scm_value
cons_rest(size_t req)
{
	scm_value rest = SCM_NULL;
	while (arg_stack.top > req) {
		rest = _cons(pop_arg(), rest);
	}
	return rest;
}

scm_value
_cons(scm_value car, scm_value cdr)
{
	Pair *p = alloc_cells(2);
	p->car = car;
	p->cdr = cdr;
	scm_value value = (scm_value)p;
	return (NB_PAIR << 48)|value;
}

scm_value
make_closure(void)
{
	Closure *clos = alloc_cells(arg_stack.top);
	clos->code = (klabel_t)pop_arg();
	for (size_t i = 0; arg_stack.top; ++i) {
		clos->free_vars[i] = pop_arg();
	}
	scm_value value = (scm_value)clos;
	return (NB_CLOSURE << 48)|value;
}

scm_value
make_int(i32 x)
{
	scm_value value = x;
	return (NB_INT << 48)|value;
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
		assert(!"ARG STACK OVERFLOW");
	}
}

scm_value
pop_arg(void)
{
	if (arg_stack.top > 0) {
		arg_stack.top--;
		return arg_stack.stk[arg_stack.top];
	} else {
		assert(!"ARG STACK UNDERFLOW");
	}
}

void
print_stk(void)
{
	for (size_t i = 0; i < arg_stack.top; ++i) {
		printf("[%zu] 0x%lx\n", i, arg_stack.stk[i]);
	}
}

static inline scm_value
addfx(f64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_float(x + (f64)GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float(x + get_float(y));
	} else {
		assert(!"Type error");
	}
}

static inline scm_value
addix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_int(x + GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float((f64)x + get_float(y));
	} else {
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
	}
}

static inline scm_value
subix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_int(x - GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float((f64)x - get_float(y));
	} else {
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
	}
}

static inline scm_value
mulix(i64 x, scm_value y)
{
	if (INTEGER_P(y)) {
		return make_int(x * GET_INTEGRAL(y));
	} else if (FLOAT_P(y)) {
		return make_float((f64)x * get_float(y));
	} else {
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
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
		assert(!"Type error");
	}
}

scm_value
primop_add(void)
{
	assert(chk_args(0, 1));
	scm_value sum = make_int(0);
	while (arg_stack.top) {
		sum = addxx(sum, pop_arg());
	}
	return sum;
}

Closure *
prim_add(UNUSED_ATTR Closure *self)
{
	scm_value k = pop_arg();
	push_arg(primop_add());
	TAIL_CALL(k);
}

scm_value
primop_sub(void)
{
	assert(chk_args(1, 1));
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

Closure *
prim_sub(UNUSED_ATTR Closure *self)
{
	scm_value k = pop_arg();
	push_arg(primop_sub());
	TAIL_CALL(k);
}

scm_value
primop_mul(void)
{
	assert(chk_args(0, 1));
	scm_value total = make_int(1);
	while (arg_stack.top) {
		total = mulxx(total, pop_arg());
	}
	return total;
}

Closure *
prim_mul(UNUSED_ATTR Closure *self)
{
	scm_value k = pop_arg();
	push_arg(primop_mul());
	TAIL_CALL(k);
}

scm_value
primop_div(void)
{
	assert(chk_args(1, 1));
	if (arg_stack.top == 1) {
		return divxx(make_float(1.0), pop_arg());
	}
	scm_value fst = pop_arg();
	scm_value total = make_int(0);
	while (arg_stack.top) {
		total = mulxx(total, pop_arg());
	}
	return divxx(fst, total);
}

Closure *
prim_div(UNUSED_ATTR Closure *self)
{
	scm_value k = pop_arg();
	push_arg(primop_div());
	TAIL_CALL(k);
}

scm_value
primop_less(void)
{
	assert(chk_args(0, 1));
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

Closure *
prim_less(Closure *self)
{
	scm_value k = pop_arg();
	push_arg(primop_less());
	TAIL_CALL(k);
}


static Closure *
tl_exit(UNUSED_ATTR Closure *self)
{
	scm_value x = pop_arg();
	printf("exit_value = %ld\n", GET_INTEGRAL(x));
	exit(0);
}

void
trampoline(Closure *cc)
{
	push_arg((scm_value)tl_exit);
	scm_value kexit = make_closure();
	exception_handler = GET_PTR(kexit);
	push_arg(kexit);
	for (;;) {
		cc = cc->code(cc);
	}
}
