#include <time.h>
#include <execinfo.h>
#include "sly_types.h"
#include "sly_alloc.h"
#include "opcodes.h"

#define DICT_INIT_SIZE 32
#define DICT_LOAD_FACTOR 0.70

static u8
rand_byte(void)
{
	static time_t seed = 0;
	if (!seed) {
		seed = time(NULL);
		srand(seed);
	}
	return rand();
}

#define DH(h, c) ((((h) << 5) + (h)) + (c))

static u64
hash(void *buff, size_t size)
{
	u8 *str = buff;
	u64 h = 5381;
	while (size--) {
		h = DH(h, *str++);
	}
	return h;
}

#define hash_str(str, len) hash(str, len)
#define hash_cstr(str)     hash(str, strlen(str))

static u64
hash_symbol(char *name, size_t len)
{
	u64 h = hash_str(name, len);
	for (int i = 0; i < 32; ++i) {
		h = DH(h, rand_byte());
	}
	return h;
}

void
_sly_assert(int p, char *msg, int line_number, const char *func_name, char *file_name)
{
	void *bt_info[255];
	int length = 0;
	if (!p) {
		fprintf(stderr,
				"%s:%d: Assertion failed in function %s:\n"
				"%s\n"
				"Backtrace:\n",
				file_name,
				line_number,
				func_name,
				msg);
		length = backtrace(bt_info, 255);
		backtrace_symbols_fd(bt_info, length, fileno(stderr));
		exit(1);
	}
}

void
sly_raise_exception(Sly_State *ss, int excpt, char *msg)
{
	if (ss->handle_except) {
		ss->excpt = excpt;
		ss->excpt_msg = msg;
		longjmp(ss->jbuf, 1);
	}
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

static void
print_string_lit(sly_value val)
{
	byte_vector *vec = GET_PTR(val);
	size_t len = vec->len;
	char *str = (char *)vec->elems;
	putchar('"');
	for (size_t i = 0; i < len; ++i) {
		if (str[i] == '\n') {
			putchar('\\');
			putchar('n');
		} else if (str[i] == '"') {
			putchar('\\');
			putchar('"');
		} else if (str[i] == '\\') {
			putchar('\\');
			putchar('\\');
		} else {
			putchar(str[i]);
		}
	}
	putchar('"');
}

void
sly_display(sly_value v, int lit)
{
	if (void_p(v)) {
		printf("#<void>");
		return;
	}
	if (null_p(v)) {
		printf("()");
		return;
	}
	if (bool_p(v)) {
		if (v == SLY_FALSE) {
			printf("#f");
		} else {
			printf("#t");
		}
		return;
	}
	if (pair_p(v)) {
		printf("(");
		if (!null_p(cdr(v)) && !pair_p(cdr(v))) {
			sly_display(car(v), 1);
			printf(" . ");
			sly_display(cdr(v), 1);
			printf(")");
		} else {
			while (pair_p(v)) {
				sly_display(car(v), 1);
				printf(" ");
				v = cdr(v);
			}
			if (null_p(v)) {
				printf("\b)");
			} else {
				printf(". ");
				sly_display(v, 1);
				printf(")");
			}
		}
	} else if (int_p(v)) {
		i64 n = get_int(v);
		printf("%ld", n);
	} else if (float_p(v)) {
		f64 n = get_float(v);
		printf("%g", n);
	} else if (symbol_p(v)) {
		symbol *s = GET_PTR(v);
		printf("%.*s", (int)s->len, (char *)s->name);
	} else if (string_p(v)) {
		if (lit) {
			print_string_lit(v);
		} else {
			byte_vector *s = GET_PTR(v);
			printf("%.*s", (int)s->len, (char *)s->elems);
		}
	} else if (syntax_p(v)) {
		printf("#<syntax ");
		syntax *s = GET_PTR(v);
		sly_display(s->datum, lit);
		printf(">");
	} else if (prototype_p(v)) {
		printf("#<prototype@%p>", GET_PTR(v));
	} else if (vector_p(v)) {
		vector *vec = GET_PTR(v);
		printf("#(");
		for (size_t i = 0; i < vec->len; ++i) {
			sly_display(vec->elems[i], 1);
			printf(" ");
		}
		printf("\b)");
	} else if (dictionary_p(v)) {
		vector *vec = GET_PTR(v);
		printf("#dict(");
		if (vec->cap) {
			for (size_t i = 0; i < vec->cap; ++i) {
				sly_value v = vec->elems[i];
				if (!slot_is_free(v)) {
					sly_display(vec->elems[i], 1);
					printf(" ");
				}
			}
			printf("\b)");
		} else {
			printf(")");
		}
	} else if (closure_p(v)) {
		printf("#<closure@%p>", GET_PTR(v));
		closure *clos = GET_PTR(v);
		sly_display(clos->proto, 1);
	} else if (cclosure_p(v)) {
		printf("#<cclosure@%p>", GET_PTR(v));
	} else if (upvalue_p(v)) {
		upvalue *uv = GET_PTR(v);
		printf("#<upvalue: ");
		if (uv->isclosed) {
			sly_display(uv->u.val, 1);
		} else {
			printf(" #<ref ");
			sly_display(*uv->u.ptr, 1);
			printf(">");
		}
		printf(">");

	} else {
		printf("#<UNEMPLEMENTED %#lx, %zu, %d>", v, v & TAG_MASK, TYPEOF(v));
	}
}

static u64
hash_hash(u64 h, u64 x)
{
	u8 *bytes = (u8 *)(&x);
	for (size_t i = 0; i < sizeof(x); ++i) {
		h = DH(h, bytes[i]);
	}
	return h;
}

static u64
hash_vector(sly_value v)
{
	vector *vec = GET_PTR(v);
	u64 h = 5381;
	for (size_t i = 0; i < vec->len; ++i) {
		h = hash_hash(h, sly_hash(vec->elems[i]));
	}
	return h;
}

static u64
hash_dict(sly_value v)
{
	vector *vec = GET_PTR(v);
	u64 h = 5381;
	for (size_t i = 0; i < vec->cap; ++i) {
		if (!slot_is_free(vec->elems[i])) {
			h = hash_hash(h, sly_hash(vec->elems[i]));
		}
	}
	return h;
}

static u64
hash_syntax(sly_value v)
{
	syntax *s = GET_PTR(v);
	u64 h = sly_hash(s->datum);
	return hash_hash(h, sly_hash(s->scope_set));
}

static u64
hash_pair(sly_value v)
{
	return hash_hash(car(v), cdr(v));
}

u64
sly_hash(sly_value v)
{
	if (null_p(v)) {
		return 0;
	} else if (symbol_p(v)) {
		symbol *sym = GET_PTR(v);
		return sym->hash;
	} else if (string_p(v)) {
		byte_vector *bv = GET_PTR(v);
		return hash_str(bv->elems, bv->len);
	} else if (vector_p(v)) {
		return hash_vector(v);
	} else if (dictionary_p(v)) {
		return hash_dict(v);
	} else if (syntax_p(v)) {
		return hash_syntax(v);
	} else if (pair_p(v)) {
		return hash_pair(v);
	}
	printf("Value Error: ");
	sly_display(v, 1);
	printf("\n");
	sly_assert(0, "Hash unemplemented for type");
	return 0;
}

int
symbol_eq(sly_value o1, sly_value o2)
{
	sly_assert(symbol_p(o1), "Type Error: Expected symbol");
	sly_assert(symbol_p(o2), "Type Error: Expected symbol");
	symbol *s1 = GET_PTR(o1);
	symbol *s2 = GET_PTR(o2);
	return s1->hash == s2->hash;
}

int
identifier_eq(sly_value o1, sly_value o2)
{
	if (identifier_p(o1) && identifier_p(o2)) {
		syntax *s1 = GET_PTR(o1);
		syntax *s2 = GET_PTR(o2);
		return symbol_eq(s1->datum, s2->datum);
	}
	return 0;
}

i64
get_int(sly_value v)
{
	sly_assert(int_p(v), "Type Error: Expected Integer");
	if (imm_p(v)) {
		union imm_value i;
		i.v = v;
		return i.i.val.as_int;
	}
	number *i = GET_PTR(v);
	return i->val.as_int;
}

f64
get_float(sly_value v)
{
	sly_assert(float_p(v), "Type Error: Expected Float");
	if (imm_p(v)) {
		union imm_value i;
		i.v = v;
		return i.i.val.as_float;
	}
	number *i = GET_PTR(v);
	return i->val.as_float;
}

sly_value
make_int(Sly_State *ss, i64 i)
{
	if (i >= INT32_MIN && i <= INT32_MAX) {
		/* small int */
		union imm_value v;
		v.i.type = imm_int;
		v.i.val.as_int = i;
		sly_value s = v.v;
		return (s & ~TAG_MASK) | st_imm;
	}
	number *n = gc_alloc(ss, sizeof(*n));
	n->h.type = tt_int;
	n->val.as_int = i;
	return (sly_value)n;
}

sly_value
make_float(Sly_State *ss, f64 f)
{
	number *n = gc_alloc(ss, sizeof(*n));
	n->h.type = tt_float;
	n->val.as_float = f;
	return (sly_value)n;
}

sly_value
make_small_float(Sly_State *ss, f32 f)
{
	(void)ss;
	union imm_value v;
	v.i.type = imm_int;
	v.i.val.as_float = f;
	sly_value n = v.v;
	return (n & ~TAG_MASK) | st_imm;
}

sly_value
cons(Sly_State *ss, sly_value car, sly_value cdr)
{
	pair *p = gc_alloc(ss, sizeof(*p));
	p->h.type = tt_pair;
	p->car = car;
	p->cdr = cdr;
	sly_value v = (sly_value)p;
	return (v & ~TAG_MASK) | st_pair;
}

sly_value
car(sly_value obj)
{
	sly_assert(pair_p(obj), "Type Error (car): Expected Pair");
	pair *p = GET_PTR(obj);
	return p->car;
}

sly_value
cdr(sly_value obj)
{
	sly_assert(pair_p(obj), "Type Error (cdr): Expected Pair");
	pair *p = GET_PTR(obj);
	return p->cdr;
}

void
set_car(sly_value obj, sly_value value)
{
	sly_assert(pair_p(obj), "Type Error: Expected Pair");
	pair *p = GET_PTR(obj);
	p->car = value;
}

void
set_cdr(sly_value obj, sly_value value)
{
	sly_assert(pair_p(obj), "Type Error: Expected Pair");
	pair *p = GET_PTR(obj);
	p->cdr = value;
}

sly_value
tail(sly_value obj)
{
	if (!pair_p(obj)) return obj;
	while (pair_p(cdr(obj))) {
		obj = cdr(obj);
	}
	return obj;
}

void
append(sly_value p, sly_value v)
{
	p = tail(p);
	set_cdr(p, v);
}

int
list_p(sly_value list)
{
	if (null_p(list)) {
		return 1;
	}
	if (pair_p(list)) {
		return list_p(cdr(list));
	}
	return 0;

}

sly_value
copy_list(Sly_State *ss, sly_value list)
{
	if (pair_p(list)) {
		cons(ss, car(list), cdr(list));
	}
	return list;
}

int
list_eq(sly_value o1, sly_value o2)
{
	while (pair_p(o1) && pair_p(o2)) {
		if (!sly_equal(car(o1), car(o2))) {
			return 0;
		}
		o1 = cdr(o1);
		o2 = cdr(o2);
	}
	return sly_equal(o1, o2);
}

size_t
list_len(sly_value list)
{
	size_t len = 0;
	for (; pair_p(list); list = cdr(list)) len++;
	return len;
}

int
list_contains(sly_value list, sly_value value)
{
	while (!null_p(list)) {
		if (sly_equal(car(list), value)) {
			return 1;
		}
		list = cdr(list);
	}
	return 0;
}

sly_value
list_to_vector(Sly_State *ss, sly_value list)
{
	size_t cap = list_len(list);
	sly_value vec = make_vector(ss, 0, cap);
	while (pair_p(list)) {
		vector_append(ss, vec, car(list));
		list = cdr(list);
	}
	return vec;
}

sly_value
vector_to_list(Sly_State *ss, sly_value vec)
{
	size_t len = vector_len(vec);
	if (len == 0) return SLY_NULL;
	if (len == 1) return cons(ss, vector_ref(vec, 0), SLY_NULL);
	/* construct list back to front */
	sly_value list = cons(ss, vector_ref(vec, len - 1), SLY_NULL);
	for (size_t i = len - 2; i > 0; ++i) {
		list = cons(ss, vector_ref(vec, i), list);
	}
	return cons(ss, vector_ref(vec, 0), list);
}

sly_value
make_byte_vector(Sly_State *ss, size_t len, size_t cap)
{
	sly_assert(len <= cap, "Error vector length may not exceed its capacity");
	byte_vector *vec = gc_alloc(ss, sizeof(*vec));
	vec->elems = MALLOC(cap);
	vec->h.type = tt_byte_vector;
	vec->cap = cap;
	vec->len = len;
	ss->gc->bytes += cap;
	return (sly_value)vec;
}

sly_value
byte_vector_ref(Sly_State *ss, sly_value v, size_t idx)
{
	sly_assert(vector_p(v), "Type Error: Expected byte-vector");
	byte_vector *vec = GET_PTR(v);
	sly_assert(idx < vec->len, "Error: Index out of bounds");
	return make_int(ss, vec->elems[idx]);
}

void
byte_vector_set(sly_value v, sly_value idx, sly_value value)
{
	sly_assert(byte_vector_p(v), "Type Error: Expected byte-vector");
	byte_vector *vec = GET_PTR(v);
	sly_assert(idx < vec->len, "Error: Index out of bounds");
	sly_assert(int_p(value) || byte_p(value),
			  "Type Error: Expected integer");
	i64 b = get_int(value);
	sly_assert(b <= UCHAR_MAX, "Error Number out of range 0-255");
	vec->elems[idx] = b;
}

size_t
byte_vector_len(sly_value v)
{
	sly_assert(byte_vector_p(v), "Type Error: Expected byte-vector");
	byte_vector *vec = GET_PTR(v);
	return vec->len;
}

sly_value
make_vector(Sly_State *ss, size_t len, size_t cap)
{
	sly_assert(len <= cap, "Error vector length may not exceed its capacity");
	vector *vec = gc_alloc(ss, sizeof(*vec));
	size_t bytes = sizeof(sly_value) * cap;
	vec->elems = MALLOC(bytes);
	vec->h.type = tt_vector;
	vec->cap = cap;
	vec->len = len;
	ss->gc->bytes += bytes;
	return (sly_value)vec;
}

int
vector_eq(sly_value o1, sly_value o2)
{
	if (vector_p(o1) && vector_p(o2)) {
		vector *v1 = GET_PTR(o1);
		vector *v2 = GET_PTR(o2);
		if (v1->len != v2->len) {
			return 0;
		}
		for (size_t i = 0; i < v1->len; ++i) {
			if (!sly_equal(v1->elems[i], v2->elems[i])) {
				return 0;
			}
		}
		return 1;
	}
	return 0;
}

sly_value
copy_vector(Sly_State *ss, sly_value v)
{
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	sly_value p = make_vector(ss, vec->len, vec->cap);
	for (size_t i = 0; i < vec->len; ++i) {
		vector_set(p, i, vector_ref(v, i));
	}
	return p;
}

sly_value
vector_ref(sly_value v, size_t idx)
{
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	sly_assert(idx < vec->len, "Error: Index out of bounds");
	return vec->elems[idx];
}

void
vector_set(sly_value v, size_t idx, sly_value value)
{
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	if (idx >= vec->len) {
		printf("idx :: %zu\n", idx);
		printf("vec->len :: %zu\n", vec->len);
		printf("vec->cap :: %zu\n", vec->cap);
		sly_assert(0, "Error: Index out of bounds");
	}
	vec->elems[idx] = value;
}

size_t
vector_len(sly_value v)
{
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	return vec->len;
}

static void
vector_grow(Sly_State *ss, sly_value v)
{
	/* Type check befor calling.
	 * Object must be a <vector> or <dictionary>
	 */
	vector *vec = GET_PTR(v);
	ss->gc->bytes += vec->cap;
	vec->cap *= 2;
	vec->elems = realloc(vec->elems, vec->cap * sizeof(sly_value));
	memset(&vec->elems[vec->len], 0, (vec->cap - vec->len) * sizeof(sly_value));
	sly_assert(vec->elems != NULL, "Realloc failed (vector_grow)");
}

void
vector_append(Sly_State *ss, sly_value v, sly_value value)
{
	UNUSED(ss);
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	if (vec->len >= vec->cap) {
		vector_grow(ss, v);
	}
	vec->elems[vec->len++] = value;
}

void
vector_remove(sly_value v, size_t idx)
{
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	sly_assert(idx < vec->len,
			   "Error index out of range (vector_remove)");
	if (idx + 1 < vec->len) {
		for (size_t i = idx + 1; i < vec->len; ++i) {
			vec->elems[i-1] = vec->elems[i];
		}
	}
	vec->len--;
}

sly_value
vector_pop(Sly_State *ss, sly_value v)
{
	UNUSED(ss);
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	sly_assert(vec->len > 0, "Type Error: Expected vector");
	vec->len--;
	return vec->elems[vec->len];
}

sly_value
vector_discard_values(Sly_State *ss, sly_value v)
{
	UNUSED(ss);
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	vec->len = 0;
	memset(vec->elems, 0, vec->cap * sizeof(sly_value));
	return SLY_VOID;
}

int
vector_contains(Sly_State *ss, sly_value vec, sly_value value)
{
	UNUSED(ss);
	size_t len = vector_len(vec);
	for (size_t i = 0; i < len; ++i) {
		if (sly_equal(vector_ref(vec, i), value)) {
			return 1;
		}
	}
	return 0;
}

sly_value
make_uninterned_symbol(Sly_State *ss, char *cstr, size_t len)
{
	sly_assert(len <= UCHAR_MAX,
			   "Value Error: name exceeds maximum for symbol");
	symbol *sym = gc_alloc(ss, sizeof(*sym));
	sym->name = MALLOC(len);
	sym->h.type = tt_symbol;
	sym->len = len;
	memcpy(sym->name, cstr, len);
	sym->hash = hash_symbol(cstr, len);
	ss->gc->bytes += len;
	return (sly_value)sym;
}

sly_value
make_symbol(Sly_State *ss, char *cstr, size_t len)
{
	sly_value sym = SLY_NULL;
	sly_value str = make_string(ss, cstr, len);
	sly_value interned = ss->interned;
	sly_value entry = dictionary_entry_ref(interned, str);
	if (slot_is_free(entry)) {
		sym = make_uninterned_symbol(ss, cstr, len);
		dictionary_set(ss, interned, str, sym);
	} else {
		sym = cdr(entry);
	}
	return sym;
}

sly_value
gensym(Sly_State *ss)
{
	static int sym_num = 0;
	char buf[255] = {0};
	snprintf(buf, sizeof(buf), "__gensym%d__", sym_num++);
	return make_uninterned_symbol(ss, buf, strlen(buf));
}

void
intern_symbol(Sly_State *ss, sly_value sym)
{
	sly_assert(symbol_p(sym), "Type error expected <symbol>");
	sly_value interned = ss->interned;
	symbol *s = GET_PTR(sym);
	sly_value str = make_string(ss, (char *)s->name, s->len);
	dictionary_set(ss, interned, str, sym);
}

sly_value
make_string(Sly_State *ss, char *cstr, size_t len)
{
	sly_value val = make_byte_vector(ss, len, len);
	byte_vector *vec = GET_PTR(val);
	vec->h.type = tt_string;
	memcpy(vec->elems, cstr, len);
	return val;
}

char *
string_to_cstr(sly_value s)
{
	sly_assert(string_p(s), "Type error expected <string>");
	byte_vector *v = GET_PTR(s);
	char *cstr = MALLOC(v->len + 1);
	memcpy(cstr, v->elems, v->len);
	cstr[v->len] = '\0';
	return cstr;
}

size_t
string_len(sly_value str)
{
	sly_assert(string_p(str), "Type error expected <string>");
	byte_vector *bv = GET_PTR(str);
	return bv->len;
}

sly_value
string_eq(sly_value s1, sly_value s2)
{
	sly_assert(string_p(s1) && string_p(s2), "Type error expected <string>");
	if (string_len(s1) != string_len(s2)) return 0;
	byte_vector *bv1 = GET_PTR(s1);
	byte_vector *bv2 = GET_PTR(s2);
	for (size_t i = 0; i < bv1->len; ++i) {
		if (bv1->elems[i] != bv2->elems[i]) return 0;
	}
	return 1;
}

sly_value
make_prototype(Sly_State *ss, sly_value uplist, sly_value constants, sly_value code,
			   size_t nregs, size_t nargs, size_t entry, int has_varg)
{
	prototype *proto = gc_alloc(ss, sizeof(*proto));
	proto->h.type = tt_prototype;
	proto->uplist = uplist;
	proto->K = constants;
	proto->code = code;
	proto->nregs = nregs;
	proto->nargs = nargs;
	proto->nvars = 0;
	proto->entry = entry;
	proto->has_varg = has_varg;
	proto->syntax_info = make_vector(ss, 0, 8);
	return (sly_value)proto;
}

sly_value
make_closure(Sly_State *ss, sly_value _proto)
{
	sly_assert(prototype_p(_proto), "Type Error expected prototype");
	closure *clos = gc_alloc(ss, sizeof(*clos));
	clos->h.type = tt_closure;
	prototype *proto = GET_PTR(_proto);
	size_t cap = 1 + vector_len(proto->uplist); /* + 1 for globals */
	if (proto->has_varg) cap++;
	clos->upvals = make_vector(ss, cap, cap);
	clos->proto = _proto;
	return (sly_value)clos;
}

sly_value
make_cclosure(Sly_State *ss, cfunc fn, size_t nargs, int has_varg)
{
	cclosure *clos = gc_alloc(ss, sizeof(*clos));
	clos->h.type = tt_cclosure;
	clos->fn = fn;
	clos->nargs = nargs;
	clos->has_varg = has_varg;
	return (sly_value)clos;
}

sly_value
make_continuation(Sly_State *ss, struct _stack_frame *frame, size_t pc, size_t ret_slot)
{
	continuation *cc = gc_alloc(ss, sizeof(*cc));
	cc->h.type = tt_continuation;
	cc->frame = frame;
	cc->pc = pc;
	cc->ret_slot = ret_slot;
	return (sly_value)cc;
}

int
sly_arity(sly_value proc)
{
	/* Retuns an int representing the arity of the procedure.
	 * The absolute value of arity is the number of non-optional args.
	 * If the value is negative, the procedure has a variatic tail arg.
	 */
	int arity = 0;
	if (cclosure_p(proc)) {
		cclosure *clos = GET_PTR(proc);
		arity = clos->nargs;
		if (clos->has_varg) {
			arity = -(arity + 1);
		}
	} else if (prototype_p(proc)) {
		prototype *proto = GET_PTR(proc);
		arity = proto->nargs;
		if (proto->has_varg) {
			arity = -(arity + 1);
		}
	} else if (closure_p(proc)) {
		closure *clos = GET_PTR(proc);
		prototype *proto = GET_PTR(clos->proto);
		arity = proto->nargs;
		if (proto->has_varg) {
			arity = -(arity + 1);
		}
	} else {
		sly_assert(0, "Type Error value must be a procedure type");
	}
	return arity;
}

static sly_value
addfx(Sly_State *ss, f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(ss, x + (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, x + get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
addix(Sly_State *ss, i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(ss, x + get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, (f64)x + get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_add(Sly_State *ss, sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return addix(ss, get_int(x), y);
	} else if (float_p(x)) {
		return addfx(ss, get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
subfx(Sly_State *ss, f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(ss, x - (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, x - get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
subix(Sly_State *ss, i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(ss, x - get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, (f64)x - get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_sub(Sly_State *ss, sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return subix(ss, get_int(x), y);
	} else if (float_p(x)) {
		return subfx(ss, get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
mulfx(Sly_State *ss, f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(ss, x * (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, x * get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
mulix(Sly_State *ss, i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(ss, x * get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, (f64)x * get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_mul(Sly_State *ss, sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return mulix(ss, get_int(x), y);
	} else if (float_p(x)) {
		return mulfx(ss, get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
divfx(Sly_State *ss, f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(ss, x / (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, x / get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
divix(Sly_State *ss, i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(ss, x / get_int(y));
	} else if (float_p(y)) {
		return make_float(ss, (f64)x / get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_div(Sly_State *ss, sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return divix(ss, get_int(x), y);
	} else if (float_p(x)) {
		return divfx(ss, get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_idiv(Sly_State *ss, sly_value x, sly_value y)
{
	sly_assert(int_p(x), "Type Error expected integer");
	sly_assert(int_p(y), "Type Error expected integer");
	return make_int(ss, get_int(x) / get_int(y));
}

sly_value
sly_mod(Sly_State *ss, sly_value x, sly_value y)
{
	sly_assert(int_p(x), "Type Error expected integer");
	sly_assert(int_p(y), "Type Error expected integer");
	return make_int(ss, get_int(x) % get_int(y));
}

static int
num_eqfx(f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return x  == get_int(y);
	} else if (float_p(y)) {
		return x == get_float(y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

static int
num_eqix(i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return x == get_int(y);
	} else if (float_p(y)) {
		return x == get_float(y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

int
sly_num_eq(sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return num_eqix(get_int(x), y);
	} else if (float_p(x)) {
		return num_eqfx(get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

static int
num_ltfx(f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return x  < get_int(y);
	} else if (float_p(y)) {
		return x < get_float(y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

static int
num_ltix(i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return x < get_int(y);
	} else if (float_p(y)) {
		return x < get_float(y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

int
sly_num_lt(sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return num_ltix(get_int(x), y);
	} else if (float_p(x)) {
		return num_ltfx(get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

static int
num_gtfx(f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return x  > get_int(y);
	} else if (float_p(y)) {
		return x > get_float(y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

static int
num_gtix(i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return x > get_int(y);
	} else if (float_p(y)) {
		return x > get_float(y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

int
sly_num_gt(sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return num_gtix(get_int(x), y);
	} else if (float_p(x)) {
		return num_gtfx(get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return 0;
}

int
sly_eq(sly_value o1, sly_value o2)
{
	if (symbol_p(o1) && symbol_p(o2)) {
		return symbol_eq(o1, o2);
	}
	if (number_p(o1) && number_p(o2)) {
		return sly_num_eq(o1, o2);
	}
	if (string_p(o1) && string_p(o2)) {
		return string_eq(o1, o2);
	}
	return 0;
}

#if 0

static int
contains(sly_value xs, sly_value x)
{
	if (null_p(xs)) {
		return 0;
	}
	if (sly_equal(car(xs), x)) {
		return 1;
	}
	return contains(cdr(xs), x);
}

static int
is_subset(sly_value xs, sly_value ys)
{
	if (null_p(xs)) {
		return 1;
	}
	if (null_p(ys)) {
		return 0;
	}
	if (contains(ys, car(xs))) {
		return is_subset(cdr(xs), ys);
	}
	return 0;
}

#else

static int
set_contains(sly_value set, sly_value value)
{ // check if set contains a value
	return !slot_is_free(dictionary_entry_ref(set, value));
}

static int
is_subset(sly_value set1, sly_value set2)
{ // check if set2 is a subset of set1
	if (!dictionary_p(set1) || !dictionary_p(set2)) {
		return 0;
	}
	vector *vec = GET_PTR(set2);
	sly_value entry;
	for (size_t i = 0; i < vec->cap; ++i) {
		entry = vec->elems[i];
		if (!slot_is_free(entry)
			&& !set_contains(set1, car(entry))) {
			/* if set2 has a value not found in slot1 */
			return 0;
		}
	}
	return 1;
}

#endif

int
sly_equal(sly_value o1, sly_value o2)
{
	if (syntax_p(o1) && syntax_p(o2)) {
		syntax *s1 = GET_PTR(o1);
		syntax *s2 = GET_PTR(o2);
		return ((list_len(s1->scope_set) == list_len(s2->scope_set)
				 && is_subset(s1->scope_set, s2->scope_set))
				&& sly_equal(s1->datum, s2->datum));
	}
	if (symbol_p(o1) && symbol_p(o2)) {
		return symbol_eq(o1, o2);
	}
	if (number_p(o1) && number_p(o2)) {
		return sly_num_eq(o1, o2);
	}
	if (string_p(o1) && string_p(o2)) {
		return string_eq(o1, o2);
	}
	if (vector_p(o1) && vector_p(o2)) {
		return vector_eq(o1, o2);
	}
	if (pair_p(o1) && pair_p(o2)) {
		return list_eq(o1, o2);
	}
	return o1 == o2;
}


sly_value
make_syntax(Sly_State *ss, token tok, sly_value datum)
{
	syntax *stx = gc_alloc(ss, sizeof(*stx));
	stx->h.type = tt_syntax;
	stx->tok = tok;
	stx->datum = datum;
	stx->context = 0;
	stx->scope_set = SLY_NULL;
	stx->env = SLY_VOID;
	return (sly_value)stx;
}

sly_value
syntax_to_datum(sly_value syn)
{
	sly_assert(syntax_p(syn), "Type Error expected <syntax>");
	syntax *s = GET_PTR(syn);
	return s->datum;
}

static sly_value
datum_to_syntax_rec(Sly_State *ss, sly_value id, sly_value datum)
{
	sly_assert(syntax_p(id), "Type Error expected #<syntax>");
	syntax *s = GET_PTR(id);
	token t = s->tok;
	if (null_p(datum)
		|| void_p(datum)
		|| syntax_p(datum)) {
		return datum;
	}
	if (pair_p(datum)) {
		return cons(ss,
					datum_to_syntax(ss, id, car(datum)),
					datum_to_syntax_rec(ss, id, cdr(datum)));
	}
	return make_syntax(ss, t, datum);
}

sly_value
datum_to_syntax(Sly_State *ss, sly_value id, sly_value datum)
{
	sly_assert(syntax_p(id), "Type Error expected #<syntax>");
	syntax *s = GET_PTR(id);
	token t = s->tok;
	if (null_p(datum)
		|| void_p(datum)
		|| syntax_p(datum)) {
		return datum;
	}
	sly_value stx;
	if (pair_p(datum)) {
		stx = make_syntax(ss, t,
						  cons(ss,
							   datum_to_syntax(ss, id, car(datum)),
							   datum_to_syntax_rec(ss, id, cdr(datum))));
	} else {
		stx = make_syntax(ss, t, datum);
	}
	return stx;
}

static sly_value
syntax_to_list_rec(Sly_State *ss, sly_value form)
{
	if (syntax_pair_p(form)) {
		syntax *s = GET_PTR(form);
		sly_value p = s->datum;
		return cons(ss, syntax_to_list_rec(ss, car(p)),
					syntax_to_list_rec(ss, cdr(p)));
	}
	if (pair_p(form)) {
		return cons(ss, syntax_to_list_rec(ss, car(form)),
					syntax_to_list_rec(ss, cdr(form)));
	}
	return form;
}

sly_value
syntax_to_list(Sly_State *ss, sly_value form)
{
	sly_assert(syntax_pair_p(form),
			   "Type error expected syntax pair");
	return syntax_to_list_rec(ss, form);
}

sly_value
syntax_scopes(sly_value value)
{
	sly_assert(syntax_p(value),
			   "Type error expected <syntax>");
	syntax *s = GET_PTR(value);
	return s->scope_set;
}

token
syntax_get_token(Sly_State *ss, sly_value stx)
{
	UNUSED(ss);
	sly_assert(syntax_p(stx),
			   "Type error expected <syntax>");
	syntax *s = GET_PTR(stx);
	return s->tok;
}

static sly_value
make_dictionary_sz(Sly_State *ss, size_t size)
{
	sly_value v = make_vector(ss, 0, size);
	vector *dict = GET_PTR(v);
	dict->h.type = tt_dictionary;
	for (size_t i = 0; i < dict->cap; ++i) {
		dict->elems[i] = SLY_NULL;
	}
	return v;
}

sly_value
make_dictionary(Sly_State *ss)
{
	return make_dictionary_sz(ss, DICT_INIT_SIZE);
}

int
slot_is_free(sly_value slot)
{
	return null_p(slot) || null_p(car(slot));
}

static size_t
dict_get_slot(sly_value d, sly_value key)
{
	vector *dict = GET_PTR(d);
	u64 h = sly_hash(key);
	size_t idx = h % dict->cap;
	i64 free_slot = -1;
	size_t i = idx;
	size_t end = dict->cap;
	/* search from hash index to end of array */
loop:
	for (; i < end; ++i) {
		if (null_p(dict->elems[i])) {
			if (free_slot == -1) {
				return i;
			}
			return free_slot;
		} else if (null_p(car(dict->elems[i]))) {
			if (free_slot == -1) {
				free_slot = i;
			}
		} else if (sly_equal(key, car(dict->elems[i]))) {
			return i;
		}
	}
	if (i == dict->cap) {
		/* search again from begining to hash index */
		i = 0;
		end = idx;
		goto loop;
	}
	/* we should *never* reach this point if dictionary is properly maintianed */
	sly_assert(0, "Dictionary Key Error key not found");
	return -1;
}

static void
dict_resize(Sly_State *ss, sly_value d)
{
	vector *old = GET_PTR(d);
	d = make_dictionary_sz(ss, old->cap * 2);
	vector *new = GET_PTR(d);
	for (size_t i = 0; i < old->cap; ++i) {
		sly_value entry = old->elems[i];
		if (pair_p(entry)) {
			dictionary_set(ss, d, car(entry), cdr(entry));
		}
	}
	void *tmp = old->elems;
	old->cap = new->cap;
	old->len = new->len;
	old->elems = new->elems;
	new->elems = tmp;
}

void
dictionary_set(Sly_State *ss, sly_value d, sly_value key, sly_value value)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	sly_assert(!null_p(key), "Type Error key cannot be <null>");
	sly_assert(!void_p(key), "Type Error key cannot be <void>");
	vector *dict = GET_PTR(d);
	size_t idx = dict_get_slot(d, key);
	sly_value entry = dict->elems[idx];
	if (slot_is_free(entry)) {
		dict->elems[idx] = cons(ss, key, value);
		dict->len++;
		if (((f64)dict->len / (f64)dict->cap) > DICT_LOAD_FACTOR) {
			dict_resize(ss, d);
		}
	} else {
		set_cdr(entry, value);
	}
}

sly_value
dictionary_entry_ref(sly_value d, sly_value key)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	sly_assert(!null_p(key), "Type Error key cannot be <null>");
	sly_assert(!void_p(key), "Type Error key cannot be <void>");
	size_t idx = dict_get_slot(d, key);
	vector *dict = GET_PTR(d);
	return dict->elems[idx];
}

sly_value
dictionary_ref(sly_value d, sly_value key)
{
	sly_value entry = dictionary_entry_ref(d, key);
	sly_assert(!slot_is_free(entry), "Dictionary Key Error key not found");
	return cdr(entry);
}

void
dictionary_remove(sly_value d, sly_value key)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	sly_assert(!null_p(key), "Type Error key cannot be <null>");
	sly_assert(!void_p(key), "Type Error key cannot be <void>");
	size_t idx = dict_get_slot(d, key);
	vector *dict = GET_PTR(d);
	sly_assert(!slot_is_free(dict->elems[idx]), "Dictionary Key Error key not found");
	set_car(dict->elems[idx], SLY_NULL); // null key indicates slot is free
	dict->len--;
}

void
dictionary_import(Sly_State *ss, sly_value dst, sly_value src)
{
	sly_assert(dictionary_p(dst), "Type Error expected <dictionary>");
	sly_assert(dictionary_p(src), "Type Error expected <dictionary>");
	vector *vsrc = GET_PTR(src);
	for (size_t i = 0; i < vsrc->cap; ++i) {
		sly_value entry = vsrc->elems[i];
		if (!slot_is_free(entry)) {
			dictionary_set(ss, dst, car(entry), cdr(entry));
		}
	}
}

sly_value
dictionary_get_entries(Sly_State *ss, sly_value d)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	vector *dict = GET_PTR(d);
	sly_value list = SLY_NULL;
	sly_value entry;
	for (size_t i = 0; i < dict->cap; ++i) {
		entry = dict->elems[i];
		if (!slot_is_free(entry)) {
			list = cons(ss, entry, list);
		}
	}
	return list;
}

sly_value
dictionary_get_keys(Sly_State *ss, sly_value d)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	vector *dict = GET_PTR(d);
	sly_value list = SLY_NULL;
	sly_value entry;
	for (size_t i = 0; i < dict->cap; ++i) {
		entry = dict->elems[i];
		if (!slot_is_free(entry)) {
			list = cons(ss, car(entry), list);
		}
	}
	return list;
}

sly_value
dictionary_get_values(Sly_State *ss, sly_value d)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	vector *dict = GET_PTR(d);
	sly_value list = SLY_NULL;
	sly_value entry;
	for (size_t i = 0; i < dict->cap; ++i) {
		entry = dict->elems[i];
		if (!slot_is_free(entry)) {
			list = cons(ss, cdr(entry), list);
		}
	}
	return list;
}

void
close_upvalue(sly_value _uv)
{
	sly_assert(upvalue_p(_uv), "Type Error expected <upvalue>");
	upvalue *uv = GET_PTR(_uv);
	sly_assert(uv->isclosed == 0, "Error upvalue is already closed");
	uv->isclosed = 1;
	uv->u.val = *uv->u.ptr;
	uv->next = NULL;
}

static upvalue *
_find_open_upvalue(upvalue *list, sly_value *ptr, upvalue **parent)
{
	upvalue *uv = list->next;
	if (uv == NULL) {
		return NULL;
	}
	if (uv->u.ptr == ptr) {
		if (parent) *parent = list;
		return uv;
	}
	return _find_open_upvalue(uv, ptr, parent);
}

upvalue *
find_open_upvalue(Sly_State *ss, sly_value *ptr, upvalue **parent)
{
	upvalue *list = ss->open_upvals;
	if (list == NULL) {
		return NULL;
	}
	if (list->u.ptr == ptr) {
		return list;
	}
	return _find_open_upvalue(list, ptr, parent);
}

sly_value
make_closed_upvalue(Sly_State *ss, sly_value val)
{
	upvalue *uv = gc_alloc(ss, sizeof(*uv));
	uv->h.type = tt_upvalue;
	uv->isclosed = 1;
	uv->u.val = val;
	uv->next = NULL;
	return (sly_value)uv;
}

sly_value
make_open_upvalue(Sly_State *ss, sly_value *ptr)
{
	upvalue *uv = find_open_upvalue(ss, ptr, NULL);
	if (uv == NULL) {
		uv = gc_alloc(ss, sizeof(*uv));
		uv->h.type = tt_upvalue;
		uv->isclosed = 0;
		uv->u.ptr = ptr;
		uv->next = ss->open_upvals;
		ss->open_upvals = uv;
	}
	return (sly_value)uv;
}

sly_value
upvalue_get(sly_value uv)
{
	sly_assert(upvalue_p(uv), "Type Error expected <upvalue>");
	upvalue *u = GET_PTR(uv);
	if (u->isclosed) {
		return u->u.val;
	}
	return *u->u.ptr;
}

void
upvalue_set(sly_value uv, sly_value value)
{
	sly_assert(upvalue_p(uv), "Type Error expected <upvalue>");
	upvalue *u = GET_PTR(uv);
	if (u->isclosed) {
		u->u.val = value;
	} else {
		*u->u.ptr = value;
	}
}

int
upvalue_isclosed(sly_value uv)
{
	sly_assert(upvalue_p(uv), "Type Error expected <upvalue>");
	upvalue *u = GET_PTR(uv);
	return u->isclosed;
}
