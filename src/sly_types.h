#ifndef SLY_TYPES_H_
#define SLY_TYPES_H_

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <gc.h>
#include "../common/common_def.h"
#include "lexer.h"

typedef u64 sly_value;

#define st_ptr   0x0
#define st_null  0x1
#define st_imm   0x2
#define st_false 0x3
#define st_true  0x4
#define TAG_MASK 0x7

enum imm_type {
	imm_int = 0,
	imm_byte,
	imm_float,
};

#define SLY_NULL  ((sly_value)st_null)
#define SLY_VOID  ((sly_value)0)
#define SLY_FALSE ((sly_value)st_false)
#define SLY_TRUE  ((sly_value)st_true)
#define ctobool(b) ((b) ? SLY_TRUE : SLY_FALSE)
#define booltoc(b) ((b) == SLY_FALSE ? 0 : 1)
#define HANDLE_EXCEPTION(ss, code)				\
	do {										\
		if (setjmp((ss)->jbuf)) {				\
			code								\
			END_HANDLE_EXCEPTION(ss);			\
		}       								\
		(ss)->handle_except = 1;				\
	} while (0)
#define END_HANDLE_EXCEPTION(ss)				\
	do {										\
		(ss)->handle_except = 0;				\
	} while (0)

enum exc_code { /* exception code */
	EXC_GENERIC,
	EXC_COMPILE,
	EXC_MACRO,
	EXC_TYPE,
	EXC_ARGS,
	EXC_ALLOC,
	EXC_HASH,
	EXCEPTION_COUNT,
};

typedef struct _sly_state {
	char *file_path;
	char *source_code;
	struct compile *cc;
	struct _stack_frame *frame;
	struct _upvalue *open_upvals;
	sly_value proto;
	sly_value entry_point;   /* closure */
	sly_value interned;
	jmp_buf jbuf;
	char *excpt_msg;
	int handle_except;
	int excpt;
} Sly_State;

#include "sly_compile.h"

union imm_value {
	sly_value v;
	struct {
		u8 _padding[3];
		u8 type;
		union {
			u32 as_uint;
			i32 as_int;
			f32 as_float;
			i8 as_byte;
			i8 as_char;
		} val;
	} i;
};

enum type_tag {
	tt_pair,			// 0
	tt_byte,			// 1
	tt_int,				// 2
	tt_float,			// 3
	tt_symbol,			// 4
	tt_byte_vector,		// 5
	tt_vector,			// 6
	tt_string,			// 7
	tt_dictionary,		// 8
	tt_prototype,		// 9
	tt_closure,			// 10
	tt_cclosure,		// 11
	tt_upvalue,			// 12
	tt_continuation,	// 13
	tt_syntax,			// 14
	tt_scope,			// 15
	tt_stack_frame,		// 16
	tt_user_data,       // 17
	tt_ir_closure,
};

#define OBJ_HEADER int type

typedef struct _pair {
	OBJ_HEADER;
	sly_value car;
	sly_value cdr;
} pair;

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
	sly_value alias;
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
	sly_value uplist;		// <vector> list of upval locations
	sly_value K;			// <vector> constants
	sly_value code;			// <vector> Byte code segment
	size_t entry;			// entry point
	size_t nregs;			// count of registers needed
	size_t nargs;			// count of arguments
	size_t nvars;			// count of arguments + variables
	int has_varg;			// has variable argument
	sly_value syntax_info;	// <vector> syntax
	sly_value binding;		// symbol
} prototype;

typedef struct _upvalue {
	OBJ_HEADER;
	struct _upvalue *next;
	int isclosed;
	union {
		sly_value *ptr;
		sly_value val;
	} u;
} upvalue;

typedef struct _clos {
	OBJ_HEADER;
	sly_value upvals; // captured values
	sly_value proto;  // <prototype>
} closure;

typedef struct _cont {
	OBJ_HEADER;
	struct _stack_frame *frame;
	size_t pc;
	int ret_slot;
	int nargs;
	int has_varg;
} continuation;

typedef sly_value (*cfunc)(Sly_State *ss, sly_value args);

typedef struct _cclos {
	OBJ_HEADER;
	int has_varg;
	size_t nargs; // <vector> arglist
	cfunc fn;     // pointer to c function
} cclosure;

struct scope {
	OBJ_HEADER;
	struct scope *parent; // NULL if top-level
	sly_value proto;      // <prototype>
	sly_value symtable;   // <dictionary>
	u32 level;
};

enum syntax_context {
	ctx_none		= 0,
	ctx_tail_pos	= 1,
	ctx_macro_body	= 1 << 1,
};

typedef struct _syntax {
	OBJ_HEADER;
	token tok;
	sly_value scope_set;
	sly_value datum;
	u32 context;
} syntax;

typedef struct _user_data {
	OBJ_HEADER;
	sly_value properties;    // plist
	size_t size;
	u8 data_bytes[];
} user_data;

void _sly_assert(int p, char *msg, int line_number,
				 const char *func_name, char *file_name);
void sly_raise_exception(Sly_State *ss, int excpt, char *msg);
void sly_display(sly_value v, int lit);
void sly_displayln(sly_value v);
u64 sly_hash(sly_value v);
int symbol_eq(sly_value o1, sly_value o2);
int identifier_eq(sly_value o1, sly_value o2);
i64 get_int(sly_value v);
f64 get_float(sly_value v);
i8 get_byte(sly_value v);
sly_value make_int(Sly_State *ss, i64 i);
sly_value make_byte(Sly_State *ss, i8 i);
sly_value make_float(Sly_State *ss, f64 f);
sly_value make_big_float(Sly_State *ss, f64 f);
sly_value make_small_float(Sly_State *ss, f32  f);
sly_value cons(Sly_State *ss, sly_value car, sly_value cdr);
sly_value car(sly_value obj);
sly_value cdr(sly_value obj);
void set_car(sly_value obj, sly_value value);
void set_cdr(sly_value obj, sly_value value);
sly_value tail(sly_value obj);
int list_p(sly_value list);
sly_value make_list(Sly_State *ss, size_t nelems, ...);
sly_value copy_list(Sly_State *ss, sly_value list);
int list_eq(sly_value o1, sly_value o2);
int list_member(sly_value v, sly_value xs);
sly_value list_nub(Sly_State *ss, sly_value xs);
sly_value list_union(Sly_State *ss, sly_value xs, sly_value ys);
sly_value list_append(Sly_State *ss, sly_value p, sly_value v);
void append(sly_value p, sly_value v);
size_t list_len(sly_value list);
sly_value list_ref(sly_value list, size_t idx);
int list_index_of(sly_value list, sly_value value);
sly_value list_remove(Sly_State *ss, sly_value xs, sly_value value);
sly_value list_subtract(Sly_State *ss, sly_value xs, sly_value ys);
sly_value list_reverse(Sly_State *ss, sly_value list);
sly_value list_to_vector(Sly_State *ss, sly_value list);
sly_value vector_to_list(Sly_State *ss, sly_value vec);
sly_value make_byte_vector(Sly_State *ss, size_t len, size_t cap);
sly_value list_to_byte_vector(Sly_State *ss, sly_value lst);
sly_value byte_vector_ref(Sly_State *ss, sly_value v, size_t idx);
void byte_vector_set(sly_value v, size_t idx, sly_value value);
size_t byte_vector_len(sly_value v);
sly_value make_vector(Sly_State *ss, size_t len, size_t cap);
int vector_eq(sly_value o1, sly_value o2);
sly_value copy_vector(Sly_State *ss, sly_value v);
sly_value vector_ref(sly_value v, size_t idx);
void vector_set(sly_value v, size_t idx, sly_value value);
size_t vector_len(sly_value v);
void vector_append(Sly_State *ss, sly_value v, sly_value value);
sly_value vector_pop(Sly_State *ss, sly_value v);
void vector_remove(sly_value v, size_t idx);
sly_value vector_discard_values(Sly_State *ss, sly_value v);
int vector_contains(Sly_State *ss, sly_value vec, sly_value value);
sly_value make_uninterned_symbol(Sly_State *ss, char *cstr, size_t len);
sly_value make_symbol(Sly_State *ss, char *cstr, size_t len);
sly_value gensym(Sly_State *ss, sly_value base);
sly_value gensym_from_cstr(Sly_State *ss, char *base);
sly_value get_interned_symbol(sly_value alist, char *name, size_t len);
void intern_symbol(Sly_State *ss, sly_value sym_v);
u64 symbol_hash(sly_value sym);
char *char_name_cstr(char c);
sly_value char_name(Sly_State *ss, sly_value c);
sly_value make_string(Sly_State *ss, char *cstr, size_t len);
sly_value string_from_managed_buffer(Sly_State *ss, char *buf, size_t len);
sly_value make_uninitialized_string(Sly_State *ss, size_t len);
sly_value string_ref(Sly_State *ss, sly_value v, size_t idx);
void string_set(sly_value v, size_t idx, sly_value b);
sly_value string_join(Sly_State *ss, sly_value ls, sly_value delim);
char *string_to_cstr(sly_value s);
size_t string_len(sly_value str);
int string_eq(sly_value s1, sly_value s2);
sly_value make_prototype(Sly_State *ss, sly_value uplist, sly_value constants,
						 sly_value code, size_t nregs, size_t nargs,
						 size_t entry, int has_varg);
sly_value make_closure(Sly_State *ss, sly_value _proto);
sly_value make_cclosure(Sly_State *ss, cfunc fn, size_t nargs, int has_varg);
sly_value make_continuation(Sly_State *ss,
							struct _stack_frame *frame,
							size_t pc,
							size_t ret_slot);
sly_value get_prototype(sly_value clos);
int sly_arity(sly_value proc);
sly_value sly_add(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_sub(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_mul(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_div(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_floor_div(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_mod(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_bitwise_and(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_bitwise_ior(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_bitwise_xor(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_bitwise_eqv(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_bitwise_nand(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_bitwise_nor(Sly_State *ss, sly_value x, sly_value y);
sly_value sly_bitwise_not(Sly_State *ss, sly_value x);
sly_value sly_arithmetic_shift(Sly_State *ss, sly_value v, sly_value count);
int sly_num_eq(sly_value x, sly_value y);
int sly_num_lt(sly_value x, sly_value y);
int sly_num_gt(sly_value x, sly_value y);
int sly_eq(sly_value o1, sly_value o2);
int sly_eqv(sly_value o1, sly_value o2);
int sly_equal(sly_value o1, sly_value o2);
sly_value make_syntax(Sly_State *ss, token tok, sly_value datum);
sly_value copy_syntax(Sly_State *ss, sly_value s);
sly_value syntax_to_datum(sly_value syn);
sly_value datum_to_syntax(Sly_State *ss, sly_value id, sly_value datum);
sly_value syntax_to_list(Sly_State *ss, sly_value form);
sly_value syntax_scopes(sly_value value);
token syntax_get_token(Sly_State *ss, sly_value stx);
char *symbol_to_cstr(sly_value sym);
void symbol_set_alias(sly_value sym, sly_value alias);
sly_value symbol_get_alias(sly_value sym);
sly_value make_dictionary(Sly_State *ss);
size_t dictionary_len(sly_value d);
size_t dictionary_cap(sly_value d);
sly_value dictionary_to_alist(Sly_State *ss, sly_value d);
sly_value copy_dictionary(Sly_State *ss, sly_value dict);
int slot_is_free(sly_value slot);
void dictionary_set(Sly_State *ss, sly_value d, sly_value key, sly_value value);
sly_value dictionary_entry_ref(sly_value d, sly_value key);
sly_value dictionary_ref(sly_value d, sly_value key, sly_value not_found);
void dictionary_remove(sly_value d, sly_value key);
void dictionary_import(Sly_State *ss, sly_value dst, sly_value src);
sly_value dictionary_union(Sly_State *ss, sly_value d1, sly_value d2);
sly_value dictionary_get_entries(Sly_State *ss, sly_value d);
sly_value dictionary_get_keys(Sly_State *ss, sly_value d);
sly_value dictionary_get_values(Sly_State *ss, sly_value d);
void close_upvalue(sly_value _uv);
upvalue *find_open_upvalue(Sly_State *ss, sly_value *ptr, upvalue **parent);
sly_value make_open_upvalue(Sly_State *ss, sly_value *ptr);
sly_value make_closed_upvalue(Sly_State *ss, sly_value val);
int upvalue_isclosed(sly_value uv);
sly_value upvalue_get(sly_value uv);
void upvalue_set(sly_value uv, sly_value value);
sly_value make_user_data(Sly_State *ss, size_t data_size);
void *user_data_get(sly_value v);
void user_data_set(sly_value v, void *ptr);
sly_value user_data_get_properties(sly_value v);
void user_data_set_properties(sly_value v, sly_value plist);
sly_value plist_get(sly_value plist, sly_value prop);
sly_value plist_put(Sly_State *ss, sly_value plist, sly_value prop, sly_value value);
int match_syntax(Sly_State *ss, sly_value pattern, sly_value literals,
				 sly_value form, sly_value pvars, int repeat);
sly_value get_pattern_var_names(Sly_State *ss, sly_value pattern, sly_value literals);
sly_value construct_syntax(Sly_State *ss, sly_value template, sly_value pvars,
						   sly_value names, size_t idx);

#define sly_assert(p, msg) _sly_assert(p, msg, __LINE__, __func__, __FILE__)
#define cstr_to_symbol(cstr) (make_symbol(ss, (cstr), strlen(cstr)))

/* type predicates */
#define null_p(v)        ((v) == SLY_NULL)
#define void_p(v)        ((v) == SLY_VOID)
#define ptr_p(v)         (!void_p(v) && ((v) & TAG_MASK) == st_ptr)
#define open_p(v)        ref_p(v)
#define GET_PTR(v)       ((void *)((v) & ~TAG_MASK))
#define TYPEOF(v)        (*((int *)GET_PTR(v)))
#define imm_p(v)         (((v) & TAG_MASK) == st_imm)
#define true_p(v)        (((v) & TAG_MASK) == st_true)
#define false_p(v)       (((v) & TAG_MASK) == st_false)
#define bool_p(v)        (true_p(v) || false_p(v))
#define number_p(v)      (int_p(v) || float_p(v) || byte_p(v))
#define pair_p(v)        (ptr_p(v) && TYPEOF(v) == tt_pair)
#define symbol_p(v)      (ptr_p(v) && TYPEOF(v) == tt_symbol)
#define vector_p(v)      (ptr_p(v) && TYPEOF(v) == tt_vector)
#define byte_vector_p(v) (ptr_p(v) && TYPEOF(v) == tt_byte_vector)
#define string_p(v)      (ptr_p(v) && TYPEOF(v) == tt_string)
#define dictionary_p(v)  (ptr_p(v) && TYPEOF(v) == tt_dictionary)
#define prototype_p(v)   (ptr_p(v) && TYPEOF(v) == tt_prototype)
#define closure_p(v)     (ptr_p(v) && TYPEOF(v) == tt_closure)
#define cclosure_p(v)    (ptr_p(v) && TYPEOF(v) == tt_cclosure)
#define upvalue_p(v)     (ptr_p(v) && TYPEOF(v) == tt_upvalue)
#define continuation_p(v) (ptr_p(v) && TYPEOF(v) == tt_continuation)
#define syntax_p(v)      (ptr_p(v) && TYPEOF(v) == tt_syntax)
#define user_data_p(v)   (ptr_p(v) && TYPEOF(v) == tt_user_data)
#define heap_obj_p(v)    (ptr_p(v) || pair_p(v))
#define syntax_pair_p(v) (syntax_p(v) && pair_p(syntax_to_datum(v)))
#define identifier_p(v)  (syntax_p(v) && symbol_p(syntax_to_datum(v)))
#define ir_closure_p(v)  (ptr_p(v) && TYPEOF(v) == tt_ir_closure)

static inline int
int_p(sly_value val)
{
	if (null_p(val) || void_p(val)) return 0;
	if (imm_p(val)) {
		union imm_value v;
		v.v = val;
		return v.i.type == imm_int;
	} else if (val & TAG_MASK) {
		return 0;
	}
	return TYPEOF(val) == tt_int;
}

static inline int
float_p(sly_value val)
{
	if (null_p(val) || void_p(val)) return 0;
	if (imm_p(val)) {
		union imm_value v;
		v.v = val;
		return v.i.type == imm_float;
	} else if (val & TAG_MASK) {
		return 0;
	}
	return TYPEOF(val) == tt_float;
}

static inline int
byte_p(sly_value val)
{
	if (null_p(val) || void_p(val)) return 0;
	if (imm_p(val)) {
		union imm_value v;
		v.v = val;
		return v.i.type == imm_byte;
	}  else if (val & TAG_MASK) {
		return 0;
	}
	return TYPEOF(val) == tt_byte;
	return 0;
}

#endif /* SLY_TYPES_H_ */
