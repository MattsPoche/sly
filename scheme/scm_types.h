#ifndef SCM_TYPES_H_
#define SCM_TYPES_H_

#include <assert.h>
#include "../common/common_def.h"

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
#define NB_FUNCTION	    (NAN_BITS|tt_function) // procedure with no free variables
#define SCM_NULL  (NB_PAIR << 48)
#define SCM_FALSE (NB_BOOL << 48)
#define SCM_TRUE  (SCM_FALSE + 1)
#define SCM_VOID  (NB_VOID << 48)
#define TAG_VALUE(type, val_bits) (((type) << 48)|(val_bits))
#define TYPEOF(value) (value >> 48)
#define GET_FN_PTR(value) ((klabel_t)((value) & ((1LU << 48) - 1)))
#define GET_PTR(value) \
	((void *)(((value) & ((1LU << 32) - 1)) + heap_working->buf))
#define GET_WORKING_PTR(value) GET_PTR(value)
#define GET_FREE_PTR(value) \
	((void *)(((value) & ((1LU << 32) - 1)) + heap_free->buf))
#define GET_INTEGRAL(value)  ((i32)(value & ((1LU << 32) - 1)))
#define NULL_P(value) ((value) == SCM_NULL)
#define VOID_P(value) ((value) == SCM_VOID)
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
#define FUNCTION_P(value) (TYPEOF(value) == NB_FUNCTION)
#define PROCEDURE_P(value) (CLOSURE_P(value) || FUNCTION_P(value))
#define TAIL_CALL(proc) return _tail_call(proc)
#define ITOBOOL(i) (i ? SCM_TRUE : SCM_FALSE)
#define BOOLTOI(b) (b == SCM_FALSE ? 0 : 1)

enum type_tag {
	tt_inf = 0,     // 0x0
	tt_void,		// 0x1
	tt_bool,		// 0x2
	tt_char,		// 0x3
	tt_int,			// 0x4
	tt_bigint,      // 0x5
	tt_pair,		// 0x6
	tt_symbol,		// 0x7
	tt_bytevector,	// 0x8
	tt_string,		// 0x9
	tt_vector,		// 0xa
	tt_record,		// 0xb
	tt_box,			// 0xc
	tt_closure,		// 0xd
	tt_function,	// 0xe
	TT_COUNT,
	tt_float,
};

static_assert(TT_COUNT <= 0x10);

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
	scm_value type;
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
	u8 *buf;
} Mem_Pool;

union f2u {f64 f; u64 u;};

#endif /* SCM_TYPES_H_ */
