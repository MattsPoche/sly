#include <assert.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../common/common_def.h"
#include "scm_types.h"
#include "scm_runtime.h"

struct shared_buf {
	u32 fref;
	u32 len;  // length
	i8 rc;
	i8 wc;   // is wide char
	i8 ro;   // is read-only
	u8 bytes[];
};

#define ARG_STACK_LEN 512LU
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

static inline void scm_print(scm_value value, int quote_p);

scm_value
_tail_call(scm_value proc)
{
	if (BOX_P(proc)) {
		proc = box_ref(proc);
	}
	if (!PROCEDURE_P(proc)) {
		printf("\n#x%lx\n", proc);
		scm_assert(PROCEDURE_P(proc), "type error, expected procedure");
		return SCM_VOID;
	}
	if (CONTINUATION_P(proc)) {
		pop_arg();
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

static void *
scm_copy_mem(scm_value *vptr, u32 fref, size_t sz)
{
	void *ptr = &heap_free->buf[fref];
	memcpy(ptr, GET_WORKING_PTR(*vptr), sz);
	*((u32 *)GET_WORKING_PTR(*vptr)) = fref;
	*vptr = (*vptr ^ (*vptr & ((1LU << 32) - 1))) | fref;
	heap_free->idx += sz;
	return ptr;
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
		p = scm_copy_mem(vptr, fref, sz);
		scm_collect_value(&p->car);
		scm_collect_value(&p->cdr);
	} break;
	case tt_symbol: {
		Symbol *s = GET_WORKING_PTR(*vptr);
		sz = sizeof(*s) + s->len;
		sz += mem_align_offset(sz);
		scm_copy_mem(vptr, fref, sz);
	} break;
	case tt_bytevector: {
		Bytevector *b = GET_WORKING_PTR(*vptr);
		sz = sizeof(*b) + b->len;
		sz += mem_align_offset(sz);
		scm_copy_mem(vptr, fref, sz);
	} break;
	case tt_string: {
		String *s = GET_WORKING_PTR(*vptr);
		sz = sizeof(*s);
		sz += mem_align_offset(sz);
		s = scm_copy_mem(vptr, fref, sz);
		fref += sz;
		if (s->buf->fref) {
			s->buf = GET_FREE_PTR(s->buf->fref);
			s->buf->rc++;
		} else {
			sz = sizeof(*s->buf) + s->buf->len;
			sz += mem_align_offset(sz);
			struct shared_buf *prev = s->buf;
			void *ptr = &heap_free->buf[fref];
			size_t len = sizeof(*s->buf)
				+ (s->buf->len * (s->buf->wc ? sizeof(scm_wchar) : 1));
			memcpy(ptr, s->buf, len);
			prev->fref = fref;
			heap_free->idx += sz;
			s->buf = ptr;
			s->buf->rc = 1;
			s->buf->fref = 0;
			fref += sz;
		}
	} break;
	case tt_vector: {
		Vector *v = GET_WORKING_PTR(*vptr);
		sz = sizeof(*v) + (v->len * sizeof(scm_value));
		v = scm_copy_mem(vptr, fref, sz);
		for (u32 i = 0; i < v->len; ++i) {
			scm_collect_value(&v->elems[i]);
		}
	} break;
	case tt_record: {
		Record *rec = GET_WORKING_PTR(*vptr);
		sz = sizeof(*rec) + (rec->len * sizeof(scm_value));
		rec = scm_copy_mem(vptr, fref, sz);
		scm_collect_value(&rec->meta);
		for (u32 i = 0; i < rec->len; ++i) {
			scm_collect_value(&rec->elems[i]);
		}
	} break;
	case tt_box: {
		Box *box = GET_WORKING_PTR(*vptr);
		box = scm_copy_mem(vptr, fref, sizeof(*box));
		scm_collect_value(&box->value);
	} break;
	case tt_closure: {
		Closure *clos = GET_WORKING_PTR(*vptr);
		sz = sizeof(*clos) + (clos->nfree_vars * sizeof(scm_value));
		clos = scm_copy_mem(vptr, fref, sz);
		for (u32 i = 0; i < clos->nfree_vars; ++i) {
			scm_collect_value(&clos->free_vars[i]);
		}
	} break;
	case tt_inf:
	case tt_nan:
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
	====gc_threashold = heap_working->idx;
}

void
scm_chk_heap(scm_value *cc)
{
	if (heap_working->idx > (gc_threashold + (gc_threashold / 2))) {
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

static char *
string_to_cstr(scm_value s)
{
	scm_assert(STRING_P(s), "type error expected <string>");
	String *str = GET_PTR(s);
	u32 off = str->off;
	u32 len = str->len;
	char *cstr = malloc(len + 1);
	memcpy(cstr, &str->buf->bytes[off], len);
	cstr[len] = '\0';
	return cstr;
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

static inline int
byte_p(scm_value value)
{
	if (INTEGER_P(value)) {
		u32 b = GET_INTEGRAL(value);
		return b <= UCHAR_MAX;
	}
	return 0;
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
	case tt_nan: {
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

static scm_value
char_to_integer(scm_value ch)
{
	scm_assert(CHAR_P(ch), "type error expected <character>");
	return make_int(GET_INTEGRAL(ch));
}

static scm_value
integer_to_char(scm_value i)
{
	scm_assert(INTEGER_P(i), "type error expected <character>");
	return make_char(GET_INTEGRAL(i));
}

scm_value
prim_char_to_integer(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value ch = pop_arg();
	push_arg(char_to_integer(ch));
	TAIL_CALL(k);
}

scm_value
prim_integer_to_char(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value i = pop_arg();
	push_arg(integer_to_char(i));
	TAIL_CALL(k);
}

static scm_value
make_record(u32 len, scm_value meta)
{
	Record *rec;
	scm_value value = scm_heap_alloc(sizeof(*rec) + (len * sizeof(scm_value)));
	rec = GET_PTR(value);
	rec->fref = 0;
	rec->len = len;
	rec->meta = meta;
	return (NB_RECORD << 48)|value;
}

static scm_value
record_ref(scm_value value, u32 idx)
{
	scm_assert(RECORD_P(value), "type error, expected <record>");
	Record *rec = GET_PTR(value);
	scm_assert(idx < rec->len, "error index out of bounds");
	return rec->elems[idx];
}

static scm_value
record_set(scm_value r, u32 idx, scm_value v)
{
	scm_assert(RECORD_P(r), "type error, expected <record>");
	Record *rec = GET_PTR(r);
	scm_assert(idx < rec->len, "error index out of bounds");
	rec->elems[idx] = v;
	return SCM_VOID;
}

static scm_value
record_meta_set(scm_value value, scm_value v)
{
	scm_assert(RECORD_P(value), "type error, expected <record>");
	Record *rec = GET_PTR(value);
	rec->meta = v;
	return SCM_VOID;
}

scm_value
prim_make_record(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value k = pop_arg();
	scm_value len = pop_arg();
	scm_value meta = pop_arg();
	scm_assert(INTEGER_P(len), "type error, expected <integer>");
	push_arg(make_record(GET_INTEGRAL(len), meta));
	TAIL_CALL(k);
}

scm_value
prim_record_ref(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value k = pop_arg();
	scm_value rec = pop_arg();
	scm_value idx = pop_arg();
	scm_assert(INTEGER_P(idx), "type error, expected <integer>");
	push_arg(record_ref(rec, GET_INTEGRAL(idx)));
	TAIL_CALL(k);
}

scm_value
prim_record_set(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(4, 0), "arity error");
	scm_value k = pop_arg();
	scm_value rec = pop_arg();
	scm_value idx = pop_arg();
	scm_value value = pop_arg();
	scm_assert(INTEGER_P(idx), "type error, expected <integer>");
	push_arg(record_set(rec, GET_INTEGRAL(idx), value));
	TAIL_CALL(k);
}

scm_value
prim_record_meta_ref(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value rec = pop_arg();
	scm_assert(RECORD_P(rec), "type error, expected <record>");
	Record *r = GET_PTR(rec);
	push_arg(r->meta);
	TAIL_CALL(k);
}

scm_value
prim_record_meta_set(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value k = pop_arg();
	scm_value rec = pop_arg();
	scm_value meta = pop_arg();
	push_arg(record_meta_set(rec, meta));
	TAIL_CALL(k);
}

static struct shared_buf *
make_sb(size_t len, int wc, int ro)
{
	len = len * (wc ? sizeof(scm_wchar) : 1);
	struct shared_buf *sb = GET_PTR(scm_heap_alloc(sizeof(*sb) + len));
	sb->fref = 0;
	sb->len = len;
	sb->wc = wc;
	sb->ro = ro;
	sb->rc = 1;
	return sb;
}

static struct shared_buf *
sb_narrow_to_wide(struct shared_buf *sb, u32 off, u32 len)
{
	struct shared_buf *new_sb = make_sb(len, 1, sb->ro);
	u8 *p8 = sb->bytes;
	u32 *p32 = (u32 *)new_sb->bytes;
	for (u32 i = 0; i < sb->len; ++i) {
		p32[i] = p8[off + i];
	}
	return new_sb;
}

static struct shared_buf *
make_ascii_buf(const u8 *text, size_t len)
{
	struct shared_buf *sb = make_sb(len, 0, 1);
	sb->fref = 0;
	sb->len = len;
	sb->ro = 0;
	sb->wc = 0;
	memcpy(sb->bytes, text, len);
	return sb;
}

scm_value
init_string(const STATIC_String *stc_str)
{
	String *s;
	scm_value value = scm_heap_alloc(sizeof(*s));
	s = GET_PTR(value);
	s->fref = 0;
	s->len = stc_str->len;
	s->off = 0;
	s->buf = make_ascii_buf(stc_str->elems, stc_str->len);
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
	clos->is_cont = 0;
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
		scm_assert(0, "type error expected <procedure>");
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
	if (BOX_P(value)) {
		box_set(b, box_ref(value));
	} else {
		box->value = value;
	}
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
	return (NB_INT << 48)|(value & ((1LU << 32) - 1));
}

scm_value
make_char(i64 x)
{
	scm_assert(x <= INT32_MAX, "bigint unimplemented");
	scm_assert(x >= INT32_MIN, "bigint unimplemented");
	scm_value value = x;
	return (NB_CHAR << 48)|(value & ((1LU << 32) - 1));
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

static scm_value
car(scm_value p)
{
	scm_assert(PAIR_P(p), "type error expected <pair>");
	Pair *pair = GET_PTR(p);
	return pair->car;
}

static scm_value
cdr(scm_value p)
{
	scm_assert(PAIR_P(p), "type error expected <pair>");
	Pair *pair = GET_PTR(p);
	return pair->cdr;
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
	return car(p);
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
	return cdr(p);
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
prim_list(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(1, 1), "arity error");
	scm_value k = pop_arg();
	scm_value lst = SCM_NULL;
	size_t n = arg_stack.top;
	for (size_t i = 0; i < n; ++i) {
		lst = _cons(arg_stack.stk[i], lst);
	}
	arg_stack.top = 0;
	TAIL_CALL(k);
}

static int
race(scm_value h, scm_value t, u32 *len)
{
	if (PAIR_P(h)) {
		h = cdr(h);
		if (PAIR_P(h)) {
			if (h == t) {
				return 0;
			} else {
				if (len) *len += 1;
				return race(cdr(h), cdr(t), len);
			}
		} else {
			return NULL_P(h);
		}
	} else {
		return NULL_P(h);
	}
}

scm_value
prim_list_p(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value lst = pop_arg();
	push_arg(ITOBOOL(race(lst, lst, NULL)));
	TAIL_CALL(k);
}

scm_value
prim_length(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value lst = pop_arg();
	u32 len = 0;
	scm_assert(race(lst, lst, &len), "value error expected <list>");
	push_arg(make_int(len));
	TAIL_CALL(k);
}

scm_value
prim_list_ref(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value k = pop_arg();
	scm_value lst = pop_arg();
	scm_value i = pop_arg();
	scm_assert(INTEGER_P(i), "type error expected <integer>");
	u32 idx = GET_INTEGRAL(i);
	while (idx--) {
		scm_assert(PAIR_P(lst), "type error index out of bounds");
		lst = cdr(lst);
	}
	push_arg(car(lst));
	TAIL_CALL(k);

}

static scm_value
make_vector(u32 len)
{
	Vector *vec;
	scm_value value = scm_heap_alloc(sizeof(*vec) + (len * sizeof(scm_value)));
	vec = GET_PTR(value);
	vec->fref = 0;
	vec->len = len;
	return (NB_VECTOR << 48)|value;
}

scm_value
primop_vector(void)
{
	Vector *vec;
	scm_value value = make_vector(arg_stack.top);
	vec = GET_PTR(value);
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

static scm_value
make_byevector(u32 len)
{
	Bytevector *bv;
	scm_value value = scm_heap_alloc(sizeof(*bv) + len);
	bv = GET_PTR(value);
	bv->fref = 0;
	bv->len = len;
	return (NB_BYTEVECTOR << 48)|value;
}

scm_value
primop_make_vector(void)
{
	scm_assert(chk_args(1, 1), "arity error");
	scm_value vl = pop_arg();
	scm_assert(INTEGER_P(vl), "type error, expected <integer>");
	u32 len = GET_INTEGRAL(vl);
	scm_value v = make_vector(len);
	if (arg_stack.top) {
		scm_value x = pop_arg();
		Vector *vec = GET_PTR(v);
		for (u32 i = 0; i < len; ++i) {
			vec->elems[i] = x;
		}
	}
	return v;
}

scm_value
prim_make_vector(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_make_vector());
	TAIL_CALL(k);
}

scm_value
primop_vector_len(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value v = pop_arg();
	scm_assert(VECTOR_P(v), "type error, expected <vector>");
	Vector *vec = GET_PTR(v);
	return make_int(vec->len);
}

scm_value
prim_vector_len(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_vector_len());
	TAIL_CALL(k);
}

scm_value
primop_vector_ref(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value v = pop_arg();
	scm_value i = pop_arg();
	scm_assert(VECTOR_P(v), "type error, expected <vector>");
	scm_assert(INTEGER_P(i), "type error, expected <integer>");
	Vector *vec = GET_PTR(v);
	u32 idx = GET_INTEGRAL(i);
	scm_assert(idx < vec->len, "error index out of bounds");
	return make_int(vec->elems[idx]);
}

scm_value
prim_vector_ref(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_vector_ref());
	TAIL_CALL(k);
}

scm_value
primop_vector_set(void)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value v = pop_arg();
	scm_value i = pop_arg();
	scm_value x = pop_arg();
	scm_assert(BYTEVECTOR_P(v), "type error, expected <bytevector>");
	scm_assert(INTEGER_P(i), "type error, expected <integer>");
	Vector *vec = GET_PTR(v);
	u32 idx = GET_INTEGRAL(i);
	scm_assert(idx < vec->len, "error index out of bounds");
	vec->elems[idx] = x;
	return SCM_VOID;
}

scm_value
prim_vector_set(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_vector_set());
	TAIL_CALL(k);
}

scm_value
primop_make_bytevector(void)
{
	scm_assert(chk_args(1, 1), "arity error");
	scm_value vl = pop_arg();
	scm_assert(INTEGER_P(vl), "type error, expected <integer>");
	u32 len = GET_INTEGRAL(vl);
	scm_value bv = make_byevector(len);
	if (arg_stack.top) {
		scm_value b = pop_arg();
		scm_assert(byte_p(b), "type error, expected <byte>");
		Bytevector *vec = GET_PTR(bv);
		memset(vec->elems, GET_INTEGRAL(b), len);
	}
	return bv;
}

scm_value
prim_make_bytevector(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_make_bytevector());
	TAIL_CALL(k);
}

scm_value
primop_bytevector_len(void)
{
	scm_assert(chk_args(1, 0), "arity error");
	scm_value bv = pop_arg();
	scm_assert(BYTEVECTOR_P(bv), "type error, expected <bytevector>");
	Bytevector *v = GET_PTR(bv);
	return make_int(v->len);
}

scm_value
prim_bytevector_len(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_bytevector_len());
	TAIL_CALL(k);
}

scm_value
primop_bytevector_u8_ref(void)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value bv = pop_arg();
	scm_value iv = pop_arg();
	scm_assert(BYTEVECTOR_P(bv), "type error, expected <bytevector>");
	scm_assert(INTEGER_P(iv), "type error, expected <integer>");
	Bytevector *vec = GET_PTR(bv);
	u32 idx = GET_INTEGRAL(iv);
	scm_assert(idx < vec->len, "error index out of bounds");
	return make_int(vec->elems[idx]);
}

scm_value
prim_bytevector_u8_ref(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_bytevector_u8_ref());
	TAIL_CALL(k);
}

scm_value
primop_bytevector_u8_set(void)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value bv = pop_arg();
	scm_value iv = pop_arg();
	scm_value byte = pop_arg();
	scm_assert(BYTEVECTOR_P(bv), "type error, expected <bytevector>");
	scm_assert(INTEGER_P(iv), "type error, expected <integer>");
	scm_assert(byte_p(byte), "type error, expected <byte>");
	Bytevector *vec = GET_PTR(bv);
	u32 idx = GET_INTEGRAL(iv);
	scm_assert(idx < vec->len, "error index out of bounds");
	vec->elems[idx] = GET_INTEGRAL(byte);
	return SCM_VOID;
}

scm_value
prim_bytevector_u8_set(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_bytevector_u8_set());
	TAIL_CALL(k);
}

scm_value
primop_bytevector(void)
{
	size_t len = arg_stack.top;
	scm_value value = make_byevector(len);
	Bytevector *bv = GET_PTR(value);
	for (size_t i = 0; i < len; ++i) {
		scm_value b = pop_arg();
		scm_assert(byte_p(b), "type error, expected <byte>");
		bv->elems[i] = GET_INTEGRAL(b);
	}
	return value;
}

scm_value
prim_bytevector(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_bytevector());
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

static inline scm_value
closure_to_continuation(scm_value cc)
{
	scm_assert(CLOSURE_P(cc), "Type error expected a closure");
	Closure *clos = GET_PTR(cc);
	clos->is_cont = 1;
	return cc;
}

scm_value
prim_call_with_current_continuation(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity_error");
	scm_value k = pop_arg();
	scm_value f = pop_arg();
	scm_assert(PROCEDURE_P(f), "Error Expected Procedure");
	push_arg(closure_to_continuation(k));
	push_ref(k);
	TAIL_CALL(f);
}

static scm_value
call_consumer_with_values(scm_value self)
{
	scm_value k = closure_ref(self, 0);
	scm_value consumer = closure_ref(self, 1);
	push_ref(k);
	TAIL_CALL(consumer);
}


scm_value
prim_call_with_values(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(3, 0), "arity_error");
	scm_value k = pop_arg();
	scm_value producer = pop_arg();
	scm_value consumer = pop_arg();
	scm_assert(PROCEDURE_P(producer), "type error, expected <procedure>");
	scm_assert(PROCEDURE_P(consumer), "type error, expected <procedure>");
	// Make continuation that calls `consumer' with values returned from `producer'
	// The continuation of `producer' is this continuation.
	push_arg(consumer);
	push_arg(k);
	push_arg((scm_value)call_consumer_with_values);
	push_arg(make_closure());
	TAIL_CALL(producer);
}

static inline void
push_list_args(scm_value lst)
{
	if (NULL_P(lst)) {
		return;
	} else if (PAIR_P(lst)) {
		Pair *p = GET_PTR(lst);
		push_list_args(p->cdr);
		push_arg(p->car);
	} else {
		scm_assert(0, "type error expected <pair>");
	}
}

scm_value
prim_apply(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 1), "arity_error");
	scm_value k = pop_arg();
	scm_value proc = pop_arg();
	scm_value args;
	if (arg_stack.top == 0) {
		push_arg(k);
		TAIL_CALL(proc);
	} else {
		args = arg_stack.stk[0];
		for (size_t i = 1; i < arg_stack.top; ++i) {
			args = _cons(arg_stack.stk[i], args);
		}
		arg_stack.top = 0;
		push_list_args(args);
		push_arg(k);
		TAIL_CALL(proc);
	}
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

static scm_value
make_string(size_t len, int wc, int ro)
{
	String *s;
	scm_value value = scm_heap_alloc(sizeof(*s));
	s = GET_PTR(value);
	s->buf = make_sb(len, wc, ro);
	s->fref = 0;
	s->len = len;
	s->off = 0;
	return (NB_STRING << 48)|value;
}

static inline void string_set(String *s, size_t i, scm_wchar ch);

scm_value
primop_string(void)
{
	scm_assert(chk_args(0, 1), "arity error");
	size_t len = arg_stack.top;
	int wc = 0;
	for (size_t i = 0; i < len; ++i) {
		scm_value ch = arg_stack.stk[i];
		scm_assert(CHAR_P(ch), "type error, expected <character>");
		if (GET_INTEGRAL(ch) > CHAR_MAX) {
			wc = 1;
			break;
		}
	}
	scm_value string = make_string(len, wc, 0);
	for (size_t i = 0; i < len; ++i) {
		scm_value v = pop_arg();
		scm_wchar ch = GET_INTEGRAL(v);
		string_set(GET_PTR(string), i, ch);
	}
	return string;
}

scm_value
prim_string(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string());
	TAIL_CALL(k);
}

scm_value
primop_make_string(void)
{
	scm_assert(chk_args(1, 1), "arity error");
	scm_value len = pop_arg();
	scm_value ch = SCM_FALSE;
	int wc = 0;
	scm_assert(INTEGER_P(len), "type error, expected <character>");
	if (arg_stack.top) {
		ch = pop_arg();
		scm_assert(CHAR_P(ch), "type error, expected <character>");
		if (GET_INTEGRAL(ch) > CHAR_MAX) {
			wc = 1;
		}
	}
	u32 l = GET_INTEGRAL(len);
	scm_value string = make_string(l, wc, 0);
	if (CHAR_P(ch)) {
		for (size_t i = 0; i < l; ++i) {
			string_set(GET_PTR(string), i, GET_INTEGRAL(ch));
		}
	}
	return string;
}

scm_value
prim_make_string(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_make_string());
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

static inline void *
sb_ref(struct shared_buf *buf, size_t off, size_t idx)
{
	return &buf->bytes[(off + idx) * (buf->wc ? sizeof(scm_wchar) : 1)];
}

static inline scm_wchar
string_ref(String *s, size_t i)
{
	if (s->buf->wc) {
		scm_wchar *c = sb_ref(s->buf, s->off, i);
		return *c;
	} else {
		i8 *c = sb_ref(s->buf, s->off, i);
		return *c;
	}
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
	return make_char(string_ref(s, i));
}

scm_value
prim_string_ref(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_ref());
	TAIL_CALL(k);
}

static inline struct shared_buf *
sb_copy(struct shared_buf *sb, u32 off, u32 len)
{
	len = (size_t)len * (sb->wc ? sizeof(scm_wchar) : 1);
	struct shared_buf *new_sb = make_sb(len, sb->wc, 0);
	memcpy(new_sb->bytes, sb_ref(sb, off, 0), len);
	return new_sb;
}

static inline void
string_set(String *s, size_t i, scm_wchar ch)
{
	scm_assert(s->buf->ro == 0, "Error string is read-only");
	if (ch > CHAR_MAX && (s->buf->wc == 0)) {
		s->buf = sb_narrow_to_wide(s->buf, s->off, s->len);
	} else if (s->buf->rc > 1) {
		s->buf = sb_copy(s->buf, s->off, s->len);
	}
	if (s->buf->wc) {
		scm_wchar *c = sb_ref(s->buf, s->off, i);
		*c = ch;
	} else {
		i8 *c = sb_ref(s->buf, s->off, i);
		*c = ch;
	}
}

static inline scm_value
string_copy(scm_value string, u32 start, u32 end)
{
	String *s1, *s2;
	scm_value new_string = scm_heap_alloc(sizeof(*s1));
	s1 = GET_PTR(string);
	s2 = GET_PTR(new_string);
	s2->buf = s1->buf;
	s2->fref = 0;
	s2->off = start;
	s2->len = end - start;
	return (NB_STRING << 48)|new_string;
}

scm_value
primop_string_copy(void)
{
	scm_assert(chk_args(1, 1), "arity error");
	scm_value string = pop_arg();
	scm_value start, end;
	scm_assert(STRING_P(string), "type error expected <string>");
	String *s = GET_PTR(string);
	if (arg_stack.top == 1) {
		start = pop_arg();
		scm_assert(INTEGER_P(start), "type error expected <integer>");
		return string_copy(string, GET_INTEGRAL(start), s->len);
	}
	if (arg_stack.top == 2) {
		start = pop_arg();
		end = pop_arg();
		scm_assert(INTEGER_P(start), "type error expected <integer>");
		scm_assert(INTEGER_P(end), "type error expected <integer>");
		return string_copy(string, GET_INTEGRAL(start), GET_INTEGRAL(end));
	}
	scm_assert(0, "arity error");
	return SCM_VOID;
}

scm_value
prim_string_copy(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_copy());
	TAIL_CALL(k);
}

scm_value
primop_string_set(void)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value str = pop_arg();
	scm_value idx = pop_arg();
	scm_value ch = pop_arg();
	scm_assert(STRING_P(str), "type error, expected <string>");
	scm_assert(INTEGER_P(idx), "type error, expected <integer>");
	scm_assert(CHAR_P(ch), "type error, expected <character>");
	String *s = GET_PTR(str);
	i64 i = GET_INTEGRAL(idx);
	scm_wchar c = GET_INTEGRAL(ch);
	scm_assert(i >= 0, "value error, index expected to be non-negative integer");
	scm_assert(i < s->len, "value error, index out of bounds");
	string_set(s, i, c);
	return SCM_VOID;
}

scm_value
prim_string_set(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_set());
	TAIL_CALL(k);
}

static inline int
string_eq(scm_value v1, scm_value v2)
{
	scm_assert(STRING_P(v1), "type error, expected <string>");
	scm_assert(STRING_P(v2), "type error, expected <string>");
	String *s1 = GET_PTR(v1);
	String *s2 = GET_PTR(v2);
	if (s1->len != s2->len) {
		return 0;
	}
	for (u32 i = 0; i < s1->len; ++i) {
		if (string_ref(s1, i) != string_ref(s2, i)) {
			return 0;
		}
	}
	return 1;
}

static inline int
string_less(scm_value v1, scm_value v2)
{
	scm_assert(STRING_P(v1), "type error, expected <string>");
	scm_assert(STRING_P(v2), "type error, expected <string>");
	String *s1 = GET_PTR(v1);
	String *s2 = GET_PTR(v2);
	u32 len = (s1->len < s2->len) ? s1->len : s2->len;
	for (u32 i = 0; i < len; ++i) {
		scm_wchar c1 = string_ref(s1, i);
		scm_wchar c2 = string_ref(s2, i);
		if (c1 != c2) {
			return c1 < c2;
		}
	}
	return s1->len < s2->len;
}

static inline int
string_gr(scm_value v1, scm_value v2)
{
	scm_assert(STRING_P(v1), "type error, expected <string>");
	scm_assert(STRING_P(v2), "type error, expected <string>");
	String *s1 = GET_PTR(v1);
	String *s2 = GET_PTR(v2);
	u32 len = (s1->len < s2->len) ? s1->len : s2->len;
	for (u32 i = 0; i < len; ++i) {
		scm_wchar c1 = string_ref(s1, i);
		scm_wchar c2 = string_ref(s2, i);
		if (c1 != c2) {
			return c1 > c2;
		}
	}
	return s1->len > s2->len;
}

static inline int
string_leq(scm_value v1, scm_value v2)
{
	scm_assert(STRING_P(v1), "type error, expected <string>");
	scm_assert(STRING_P(v2), "type error, expected <string>");
	String *s1 = GET_PTR(v1);
	String *s2 = GET_PTR(v2);
	u32 len = (s1->len < s2->len) ? s1->len : s2->len;
	for (u32 i = 0; i < len; ++i) {
		scm_wchar c1 = string_ref(s1, i);
		scm_wchar c2 = string_ref(s2, i);
		if (c1 != c2) {
			return c1 < c2;
		}
	}
	return s1->len <= s2->len;
}

static inline int
string_geq(scm_value v1, scm_value v2)
{
	scm_assert(STRING_P(v1), "type error, expected <string>");
	scm_assert(STRING_P(v2), "type error, expected <string>");
	String *s1 = GET_PTR(v1);
	String *s2 = GET_PTR(v2);
	u32 len = (s1->len < s2->len) ? s1->len : s2->len;
	for (u32 i = 0; i < len; ++i) {
		scm_wchar c1 = string_ref(s1, i);
		scm_wchar c2 = string_ref(s2, i);
		if (c1 != c2) {
			return c1 > c2;
		}
	}
	return s1->len >= s2->len;
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

scm_value
primop_string_less(void)
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
		if (!string_less(fst, pop_arg())) {
			return SCM_FALSE;
		}
	}
	return SCM_TRUE;
}

scm_value
prim_string_less(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_less());
	TAIL_CALL(k);
}

scm_value
primop_string_gr(void)
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
		if (!string_gr(fst, pop_arg())) {
			return SCM_FALSE;
		}
	}
	return SCM_TRUE;
}

scm_value
prim_string_gr(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_gr());
	TAIL_CALL(k);
}

scm_value
primop_string_leq(void)
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
		if (!string_leq(fst, pop_arg())) {
			return SCM_FALSE;
		}
	}
	return SCM_TRUE;
}

scm_value
prim_string_leq(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_leq());
	TAIL_CALL(k);
}

scm_value
primop_string_geq(void)
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
		if (!string_geq(fst, pop_arg())) {
			return SCM_FALSE;
		}
	}
	return SCM_TRUE;
}

scm_value
prim_string_geq(UNUSED_ATTR scm_value self)
{
	scm_value k = pop_arg();
	push_arg(primop_string_geq());
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
		if (quote_p) putchar('"');
		for (size_t i = 0; i < s->len; ++i) {
			scm_wchar ch = string_ref(s, i);
			putchar(ch);
		}
		if (quote_p) putchar('"');
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
		printf("#u8(");
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
prim_open_fd_ro(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value file_path = pop_arg();
	scm_assert(STRING_P(file_path), "type error expected <string>");
	char *fp = string_to_cstr(file_path);
	printf("%s\n", fp);
	int fd = open(fp, O_RDONLY);
	free(fp);
	scm_assert(fd > 0, strerror(errno));
	push_arg(make_int(fd));
	TAIL_CALL(k);
}

scm_value
prim_close_fd(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value val = pop_arg();
	scm_assert(INTEGER_P(val), "type error expected <integer>");
	scm_assert(close(GET_INTEGRAL(val)) == 0, strerror(errno));
	TAIL_CALL(k);
}

scm_value
prim_read_fd(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value k = pop_arg();
	scm_value fd = pop_arg();
	scm_value val = pop_arg();
	scm_assert(INTEGER_P(fd), "type error expected <integer>");
	scm_assert(BYTEVECTOR_P(val), "type error expected <bytevector>");
	int fildes = GET_INTEGRAL(fd);
	Bytevector *vec = GET_PTR(val);
	scm_assert(read(fildes, vec->elems, vec->len) >= 0, strerror(errno));
	push_arg(val);
	TAIL_CALL(k);
}

scm_value
prim_c_open_file_object(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(3, 0), "arity error");
	scm_value k = pop_arg();
	scm_value file_path = pop_arg();
	scm_value mode = pop_arg();
	scm_assert(STRING_P(file_path), "type error expected <string>");
	scm_assert(STRING_P(mode), "type error expected <string>");
	char *fp = string_to_cstr(file_path);
	char *m = string_to_cstr(mode);
	FILE *file = fopen(fp, m);
	free(fp);
	free(m);
	scm_assert(file != NULL, strerror(errno));
	push_arg((scm_value)file);
	TAIL_CALL(k);
}

scm_value
prim_c_close_file_object(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value f = pop_arg();
	fclose((FILE *)f);
	TAIL_CALL(k);
}

scm_value
prim_write(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value value = pop_arg();
	if (RECORD_P(value)) {
		Record *rec = GET_PTR(value);
		if (RECORD_P(rec->meta)) {
			Record *meta = GET_PTR(rec->meta);
			push_arg(value);
			push_arg(k);
			if (PROCEDURE_P(meta->elems[2])) {
				TAIL_CALL(meta->elems[2]);
			}
		}
		printf("($ N/A)");
	} else {
		scm_print(value, 1);
	}
	push_arg(SCM_VOID);
	TAIL_CALL(k);
}

scm_value
prim_display(UNUSED_ATTR scm_value self)
{
	scm_assert(chk_args(2, 0), "arity error");
	scm_value k = pop_arg();
	scm_value value = pop_arg();
	if (RECORD_P(value)) {
		Record *rec = GET_PTR(value);
		if (RECORD_P(rec->meta)) {
			Record *meta = GET_PTR(rec->meta);
			if (PROCEDURE_P(meta->elems[2])) {
				push_arg(value);
				push_arg(k);
				TAIL_CALL(meta->elems[2]);
			}
		}
		printf("($ N/A)");
	} else {
		scm_print(value, 0);
	}
	push_arg(SCM_VOID);
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
	/* scm_value x = pop_arg(); */
	/* if (!VOID_P(x)) { */
	/* 	push_arg(x); */
	/* 	primop_write(); */
	/* 	primop_newline(); */
	/* } */
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
	printf("%s\n", name);
	scm_assert(0, "error undefined identifier");
	return SCM_VOID;
}

scm_value
scm_runtime_load_dynamic(void)
{
	push_arg(module_entry("cons", prim_cons));
	push_arg(module_entry("car", prim_car));
	push_arg(module_entry("cdr", prim_cdr));
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
	/* char */
	push_arg(module_entry("char->integer", prim_char_to_integer));
	push_arg(module_entry("integer->char", prim_integer_to_char));
	/* list */
	push_arg(module_entry("list", prim_list));
	push_arg(module_entry("list?", prim_list_p));
	push_arg(module_entry("length", prim_length));
	push_arg(module_entry("list-ref", prim_list_ref));
	/* string */
	push_arg(module_entry("string", prim_string));
	push_arg(module_entry("make-string", prim_make_string));
	push_arg(module_entry("string-copy", prim_string_copy));
	push_arg(module_entry("string-length", prim_string_len));
	push_arg(module_entry("string-ref", prim_string_ref));
	push_arg(module_entry("string-set!", prim_string_set));
	push_arg(module_entry("string=?", prim_string_eq));
	push_arg(module_entry("string<?", prim_string_less));
	push_arg(module_entry("string>?", prim_string_gr));
	push_arg(module_entry("string<=?", prim_string_leq));
	push_arg(module_entry("string>=?", prim_string_geq));
	/* bytevector */
	push_arg(module_entry("bytevector", prim_bytevector));
	push_arg(module_entry("make-bytevector", prim_make_bytevector));
	push_arg(module_entry("bytevector-length", prim_bytevector_len));
	push_arg(module_entry("bytevector-u8-ref", prim_bytevector_u8_ref));
	push_arg(module_entry("bytevector-u8-set!", prim_bytevector_u8_set));
	/* vector */
	push_arg(module_entry("vector", prim_vector));
	push_arg(module_entry("make-vector", prim_make_vector));
	push_arg(module_entry("vector-length", prim_vector_len));
	push_arg(module_entry("vector-ref", prim_vector_ref));
	push_arg(module_entry("vector-set!", prim_vector_set));
	/* record */
	push_arg(module_entry("make-record", prim_make_record));
	push_arg(module_entry("record-ref", prim_record_ref));
	push_arg(module_entry("record-set!", prim_record_set));
	push_arg(module_entry("record-meta-ref", prim_record_meta_ref));
	push_arg(module_entry("record-meta-set!", prim_record_meta_set));
	/* IO */
	push_arg(module_entry("open-fd-ro", prim_open_fd_ro));
	push_arg(module_entry("read-fd", prim_read_fd));
	push_arg(module_entry("close-fd", prim_close_fd));
	push_arg(module_entry("c/open-file-object", prim_c_open_file_object));
	push_arg(module_entry("c/close-file-object", prim_c_close_file_object));
	push_arg(module_entry("newline", prim_newline));
	push_arg(module_entry("display", prim_display));
	push_arg(module_entry("write", prim_write));
	push_arg(module_entry("call-with-current-continuation",
						  prim_call_with_current_continuation));
	push_arg(module_entry("call/cc", prim_call_with_current_continuation));
	push_arg(module_entry("call-with-values", prim_call_with_values));
	push_arg(module_entry("apply", prim_apply));
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
