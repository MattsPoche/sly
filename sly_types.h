#ifndef SLY_TYPES_H_
#define SLY_TYPES_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "lexer.h"

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;
typedef uintptr_t sly_value;

#define ARR_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#define st_ptr         0x0
#define st_pair        0x1
#define st_imm         0x2
#define st_bool        0x4
#define TAG_MASK       0x7

#define imm_int   0
#define imm_byte  1
#define imm_float 2

#define SLY_NULL  ((sly_value)st_pair)
#define SLY_VOID  ((sly_value)0)
#define SLY_FALSE ((sly_value)st_bool)
#define SLY_TRUE  ((sly_value)((UINT64_MAX^TAG_MASK)|st_bool))
#define ctobool(b) ((b) ? SLY_TRUE : SLY_FALSE)

struct imm_value {
	u8 _padding[3];
	u8 type;
	union {
		u32 as_uint;
		i32 as_int;
		f32 as_float;
	} val;
};

enum type_tag {
	tt_byte,
	tt_int,
	tt_float,
	tt_symbol,
	tt_byte_vector,
	tt_vector,
	tt_string,
	tt_dictionary,
	tt_prototype,
	tt_closure,
	tt_cclosure,
	tt_syntax,
};

typedef struct _pair {
	sly_value car;
	sly_value cdr;
} pair;

#define OBJ_HEADER int type, gcinfo

typedef struct _number {
	OBJ_HEADER;
	union {
		sly_value as_ptr;
		u64 as_uint;
		i64 as_int;
		f64 as_float;
	} val;
} number;

typedef struct _symbol {
	OBJ_HEADER;
	u64 hash;
	size_t len;
	u8 *name;
} symbol;

typedef struct _byte_vector {
	OBJ_HEADER;
	size_t len;
	size_t cap;
	u8 *elems;
} byte_vector;

typedef struct _vector {
	OBJ_HEADER;
	size_t len;
	size_t cap;
	sly_value *elems;
} vector;

typedef struct _proto {
	OBJ_HEADER;
	sly_value uplist;   // <byte-vector> list of upvals
	sly_value K;        // <vector> constants
	sly_value code;     // <vector> Byte code segment
	size_t entry;       // entry point
	size_t nregs;       // count of registers needed
	size_t nargs;       // count of arguments
	int has_varg;       // has variable argument
} prototype;

typedef struct _clos {
	OBJ_HEADER;
	sly_value upvals; // captured values
	sly_value proto;  // <prototype>
	size_t arg_idx;   // position in upvals where args are stored
} closure;

typedef sly_value (*cfunc)(sly_value args);

typedef struct _cclos {
	OBJ_HEADER;
	size_t nargs; // <vector> arglist
	cfunc fn;       // pointer to c function
	int has_varg;
} cclosure;

typedef struct _syntax {
	OBJ_HEADER;
	token tok;
	size_t __placeholder;
	sly_value datum;
} syntax;

u64 sly_hash(sly_value v);
int symbol_eq(sly_value o1, sly_value o2);
i64 get_int(sly_value v);
f64 get_float(sly_value v);
sly_value make_int(i64 i);
sly_value make_float(f64 f);
sly_value make_small_float(f32  f);
sly_value cons(sly_value car, sly_value cdr);
sly_value car(sly_value obj);
sly_value cdr(sly_value obj);
void set_car(sly_value obj, sly_value value);
void set_cdr(sly_value obj, sly_value value);
sly_value tail(sly_value obj);
void append(sly_value p, sly_value v);
size_t list_len(sly_value list);
sly_value list_to_vector(sly_value list);
sly_value vector_to_list(sly_value vec);
sly_value make_byte_vector(size_t len, size_t cap);
sly_value byte_vector_ref(sly_value v, size_t idx);
void byte_vector_set(sly_value v, size_t idx, sly_value value);
size_t byte_vector_len(sly_value v);
sly_value make_vector(size_t len, size_t cap);
sly_value copy_vector(sly_value v);
sly_value vector_ref(sly_value v, size_t idx);
void vector_set(sly_value v, size_t idx, sly_value value);
size_t vector_len(sly_value v);
void vector_append(sly_value v, sly_value value);
sly_value make_uninterned_symbol(char *cstr, size_t len);
sly_value make_symbol(sly_value interned, char *cstr, size_t len);
sly_value get_interned_symbol(sly_value alist, char *name, size_t len);
void intern_symbol(sly_value interned, sly_value sym_v);
sly_value make_string(char *cstr, size_t len);
sly_value make_prototype(sly_value uplist, sly_value constants, sly_value code,
						 size_t nregs, size_t nargs, size_t entry, int has_varg);
sly_value make_closure(sly_value _proto);
sly_value make_cclosure(cfunc fn, size_t nargs, int has_varg);
sly_value sly_add(sly_value x, sly_value y);
sly_value sly_sub(sly_value x, sly_value y);
sly_value sly_mul(sly_value x, sly_value y);
sly_value sly_div(sly_value x, sly_value y);
sly_value sly_idiv(sly_value x, sly_value y);
sly_value sly_mod(sly_value x, sly_value y);
sly_value make_syntax(token tok, sly_value datum);
sly_value syntax_to_datum(sly_value syn);
sly_value make_dictionary(void);
void dictionary_set(sly_value d, sly_value key, sly_value value);
sly_value dictionary_entry_ref(sly_value d, sly_value key);
sly_value dictionary_ref(sly_value d, sly_value key);
void dictionary_remove(sly_value d, sly_value key);

#define cstr_to_symbol(cstr) (make_symbol(interned, (cstr), strlen(cstr)))

/* type predicates */
#define ptr_p(v)         (((v) & TAG_MASK) == st_ptr)
#define GET_PTR(v)       ((void *)((v) & ~TAG_MASK))
#define TYPEOF(v)        (*((enum type_tag *)GET_PTR(v)))
#define null_p(v)        ((v) == SLY_NULL)
#define void_p(v)        ((v) == SLY_VOID)
#define pair_p(v)        (!null_p(v) && ((v) & TAG_MASK) == st_pair)
#define imm_p(v)         (((v) & TAG_MASK) == st_imm)
#define bool_p(v)        (((v) & TAG_MASK) == st_bool)
#define number_p(v)      (int_p(v) || float_p(v) || byte_p(v))
#define symbol_p(v)      (ptr_p(v) && TYPEOF(v) == tt_symbol)
#define vector_p(v)      (ptr_p(v) && TYPEOF(v) == tt_vector)
#define byte_vector_p(v) (ptr_p(v) && TYPEOF(v) == tt_byte_vector)
#define string_p(v)      (ptr_p(v) && TYPEOF(v) == tt_string)
#define dictionary_p(v)  (ptr_p(v) && TYPEOF(v) == tt_dictionary)
#define prototype_p(v)   (ptr_p(v) && TYPEOF(v) == tt_prototype)
#define closure_p(v)     (ptr_p(v) && TYPEOF(v) == tt_closure)
#define cclosure_p(v)    (ptr_p(v) && TYPEOF(v) == tt_cclosure)
#define syntax_p(v)      (ptr_p(v) && TYPEOF(v) == tt_syntax)

static inline int
int_p(sly_value val)
{
	if (imm_p(val)) {
		struct imm_value *v = (struct imm_value *)(&val);
		return v->type == imm_int;
	}
	return TYPEOF(val) == tt_int;
}

static inline int
float_p(sly_value val)
{
	if (imm_p(val)) {
		struct imm_value *v = (struct imm_value *)(&val);
		return v->type == imm_float;
	}
	return TYPEOF(val) == tt_float;
}

static inline int
byte_p(sly_value val)
{
	if (imm_p(val)) {
		struct imm_value *v = (struct imm_value *)(&val);
		return v->type == imm_byte;
	}
	return TYPEOF(val) == tt_byte;
	return 0;
}

static inline void
sly_assert(int p, char *msg)
{
	if (!p) {
		fprintf(stderr, "%s\n", msg);
		exit(1);
	}
}

static inline void *
sly_alloc(size_t size)
{
	void *ptr = malloc(size);
	sly_assert(ptr != NULL, "Error malloc failed");
	return ptr;
}

#endif /* SLY_TYPES_H_ */
