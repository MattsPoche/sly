#include <time.h>
#include "sly_types.h"

#define DICT_INIT_SIZE 32
#define DICT_LOAD_FACTOR 0.70

static sly_value dictionary_entry_ref_by_hash(sly_value d, u64 h);

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
sly_assert(int p, char *msg)
{
	if (!p) {
		fprintf(stderr, "%s\n", msg);
		exit(1);
	}
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
	}
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

i64
get_int(sly_value v)
{
	sly_assert(int_p(v), "Type Error: Expected Integer");
	if (imm_p(v)) {
		struct imm_value *i = (struct imm_value *)(&v);
		return i->val.as_int;
	}
	number *i = GET_PTR(v);
	return i->val.as_int;
}

f64
get_float(sly_value v)
{
	sly_assert(float_p(v), "Type Error: Expected Float");
	if (imm_p(v)) {
		struct imm_value *i = (struct imm_value *)(&v);
		return i->val.as_float;
	}
	number *i = GET_PTR(v);
	return i->val.as_float;
}

sly_value
make_int(i64 i)
{
	if (i >= INT32_MIN && i <= INT32_MAX) {
		/* small int */
		struct imm_value v;
		v.type = imm_int;
		v.val.as_int = i;
		sly_value s = *((sly_value *)(&v));
		return (s & ~TAG_MASK) | st_imm;
	}
	number *n = sly_alloc(sizeof(*n));
	n->type = tt_int;
	n->val.as_int = i;
	return (sly_value)n;
}

sly_value
make_float(f64 f)
{
	number *n = sly_alloc(sizeof(*n));
	n->type = tt_float;
	n->val.as_float = f;
	return (sly_value)n;
}

sly_value
make_small_float(f32  f)
{
	struct imm_value v;
	v.type = imm_int;
	v.val.as_float = f;
	return *((sly_value *)(&v));
}

sly_value
cons(sly_value car, sly_value cdr)
{
	pair *p = sly_alloc(sizeof(*p));
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
	for (;;) {
		if (!pair_p(cdr(obj))) {
			return obj;
		}
		obj = cdr(obj);
	}
	return SLY_NULL;
}

void
append(sly_value p, sly_value v)
{
	p = tail(p);
	set_cdr(p, v);
}

size_t
list_len(sly_value list)
{
	size_t len = 0;
	for (; pair_p(list); list = cdr(list));
	return len;
}

sly_value
list_to_vector(sly_value list)
{
	size_t cap = list_len(list);
	sly_value vec = make_vector(0, cap);
	while (pair_p(list)) {
		vector_append(vec, car(list));
		list = cdr(list);
	}
	return vec;
}

sly_value
vector_to_list(sly_value vec)
{
	size_t len = vector_len(vec);
	if (len == 0) return SLY_NULL;
	if (len == 1) return cons(vector_ref(vec, 0), SLY_NULL);
	/* construct list back to front */
	sly_value list = cons(vector_ref(vec, len - 1), SLY_NULL);
	for (size_t i = len - 2; i > 0; ++i) {
		list = cons(vector_ref(vec, i), list);
	}
	return cons(vector_ref(vec, 0), list);
}

sly_value
make_byte_vector(size_t len, size_t cap)
{
	sly_assert(len <= cap, "Error vector length may not exceed its capacity");
	byte_vector *vec = sly_alloc(sizeof(*vec));
	vec->elems = sly_alloc(cap);
	vec->type = tt_byte_vector;
	vec->cap = cap;
	vec->len = len;
	return (sly_value)vec;
}

sly_value
byte_vector_ref(sly_value v, size_t idx)
{
	sly_assert(vector_p(v), "Type Error: Expected byte-vector");
	byte_vector *vec = GET_PTR(v);
	sly_assert(idx < vec->len, "Error: Index out of bounds");
	return make_int(vec->elems[idx]);
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
make_vector(size_t len, size_t cap)
{
	sly_assert(len <= cap, "Error vector length may not exceed its capacity");
	vector *vec = sly_alloc(sizeof(*vec));
	vec->elems = sly_alloc(sizeof(sly_value) * cap);
	vec->type = tt_vector;
	vec->cap = cap;
	vec->len = len;
	return (sly_value)vec;
}

sly_value
copy_vector(sly_value v)
{
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	sly_value p = make_vector(vec->len, vec->cap);
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
vector_grow(sly_value v)
{
	/* Type check befor calling.
	 * Object must be a <vector> or <dictionary>
	 */
	vector *vec = GET_PTR(v);
	size_t size = vec->cap * sizeof(sly_value);
	void *ptr = vec->elems;
	vec->cap *= 2;
	vec->elems = sly_alloc(vec->cap * sizeof(sly_value));
	memcpy(vec->elems, ptr, size);
	free(ptr);
}

void
vector_append(sly_value v, sly_value value)
{
	sly_assert(vector_p(v), "Type Error: Expected vector");
	vector *vec = GET_PTR(v);
	if (vec->len >= vec->cap) {
		vector_grow(v);
	}
	vec->elems[vec->len++] = value;
}

sly_value
make_uninterned_symbol(char *cstr, size_t len)
{
	sly_assert(len <= UCHAR_MAX,
			   "Value Error: name exceeds maximum for symbol");
	symbol *sym = sly_alloc(sizeof(*sym));
	sym->name = sly_alloc(len);
	sym->type = tt_symbol;
	sym->len = len;
	memcpy(sym->name, cstr, len);
	sym->hash = hash_symbol(cstr, len);
	return (sly_value)sym;
}

sly_value
make_symbol(sly_value interned, char *cstr, size_t len)
{
	u64 h = hash_str(cstr, len);
	sly_value entry = dictionary_entry_ref_by_hash(interned, h);
	sly_value str, sym = SLY_NULL;
	if (slot_is_free(entry)) {
		str = make_string(cstr, len);
		sym = make_uninterned_symbol(cstr, len);
		dictionary_set(interned, str, sym);
	} else {
		sym = cdr(entry);
	}
	return sym;
}

void
intern_symbol(sly_value interned, sly_value sym)
{
	sly_assert(symbol_p(sym), "Type error expected <symbol>");
	symbol *s = GET_PTR(sym);
	sly_value str = make_string((char *)s->name, s->len);
	dictionary_set(interned, str, sym);
}

sly_value
make_string(char *cstr, size_t len)
{
	sly_value val = make_byte_vector(len, len);
	byte_vector *vec = GET_PTR(val);
	vec->type = tt_string;
	memcpy(vec->elems, cstr, len);
	return val;
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
make_prototype(sly_value uplist, sly_value constants, sly_value code,
			   size_t nregs, size_t nargs, size_t entry, int has_varg)
{
	prototype *proto = sly_alloc(sizeof(*proto));
	proto->type = tt_prototype;
	proto->uplist = uplist;
	proto->K = constants;
	proto->code = code;
	proto->nregs = nregs;
	proto->nargs = nargs;
	proto->entry = entry;
	proto->has_varg = has_varg;
	return (sly_value)proto;
}

sly_value
make_closure(sly_value _proto)
{
	sly_assert(prototype_p(_proto), "Type Error expected prototype");
	closure *clos = sly_alloc(sizeof(*clos));
	clos->type = tt_closure;
	prototype *proto = GET_PTR(_proto);
	clos->arg_idx = 1;
	size_t cap = clos->arg_idx + proto->nargs;
	clos->upvals = make_vector(0, cap);
	clos->proto = _proto;
	return (sly_value)clos;
}

sly_value
make_cclosure(cfunc fn, size_t nargs, int has_varg)
{
	cclosure *clos = sly_alloc(sizeof(*clos));
	clos->type = tt_cclosure;
	clos->fn = fn;
	clos->nargs = nargs;
	clos->has_varg = has_varg;
	return (sly_value)clos;
}

static sly_value
addfx(f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(x + (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(x + get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
addix(i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(x + get_int(y));
	} else if (float_p(y)) {
		return make_float((f64)x + get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_add(sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return addix(get_int(x), y);
	} else if (float_p(x)) {
		return addfx(get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
subfx(f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(x - (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(x - get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
subix(i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(x - get_int(y));
	} else if (float_p(y)) {
		return make_float((f64)x - get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_sub(sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return subix(get_int(x), y);
	} else if (float_p(x)) {
		return subfx(get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
mulfx(f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(x * (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(x * get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
mulix(i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(x * get_int(y));
	} else if (float_p(y)) {
		return make_float((f64)x * get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_mul(sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return mulix(get_int(x), y);
	} else if (float_p(x)) {
		return mulfx(get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
divfx(f64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_float(x / (f64)get_int(y));
	} else if (float_p(y)) {
		return make_float(x / get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

static sly_value
divix(i64 x, sly_value y)
{
	sly_assert(number_p(y), "Type Error expected number");
	if (int_p(y)) {
		return make_int(x / get_int(y));
	} else if (float_p(y)) {
		return make_float((f64)x / get_float(y));
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_div(sly_value x, sly_value y)
{
	sly_assert(number_p(x), "Type Error expected number");
	if (int_p(x)) {
		return divix(get_int(x), y);
	} else if (float_p(x)) {
		return divfx(get_float(x), y);
	}
	sly_assert(0, "Error Unreachable");
	return SLY_NULL;
}

sly_value
sly_idiv(sly_value x, sly_value y)
{
	sly_assert(int_p(x), "Type Error expected integer");
	sly_assert(int_p(y), "Type Error expected integer");
	return make_int(get_int(x) / get_int(y));
}

sly_value
sly_mod(sly_value x, sly_value y)
{
	sly_assert(int_p(x), "Type Error expected integer");
	sly_assert(int_p(y), "Type Error expected integer");
	return make_int(get_int(x) % get_int(y));
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

sly_value
make_syntax(token tok, sly_value datum)
{
	syntax *stx = sly_alloc(sizeof(*stx));
	stx->type = tt_syntax;
	stx->tok = tok;
	stx->datum = datum;
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
make_dictionary_sz(size_t size)
{
	sly_value v = make_vector(0, size);
	vector *dict = GET_PTR(v);
	dict->type = tt_dictionary;
	for (size_t i = 0; i < dict->cap; ++i) {
		dict->elems[i] = SLY_NULL;
	}
	return v;
}

sly_value
make_dictionary(void)
{
	return make_dictionary_sz(DICT_INIT_SIZE);
}

int
slot_is_free(sly_value slot)
{
	return null_p(slot) || null_p(car(slot));
}

static size_t
dict_get_slot(sly_value d, u64 h)
{
	vector *dict = GET_PTR(d);
	size_t idx = h % dict->cap;
	ssize_t free_slot = -1;
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
		} else if (h == sly_hash(car(dict->elems[i]))) {
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
dict_resize(sly_value d)
{
	vector *old = GET_PTR(d);
	d = make_dictionary_sz(old->cap * 2);
	vector *new = GET_PTR(d);
	for (size_t i = 0; i < old->cap; ++i) {
		sly_value entry = old->elems[i];
		if (pair_p(entry)) {
			dictionary_set(d, car(entry), cdr(entry));
		}
	}
	free(old->elems);
	*old = *new;
}

void
dictionary_set(sly_value d, sly_value key, sly_value value)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	sly_assert(!null_p(key), "Type Error key cannot be <null>");
	sly_assert(!void_p(key), "Type Error key cannot be <void>");
	vector *dict = GET_PTR(d);
	u64 h = sly_hash(key);
	size_t idx = dict_get_slot(d, h);
	sly_value entry = dict->elems[idx];
	if (slot_is_free(entry)) {
		dict->elems[idx] = cons(key, value);
		dict->len++;
		if (((f64)dict->len / (f64)dict->cap) > DICT_LOAD_FACTOR) {
			dict_resize(d);
		}
	} else {
		set_cdr(entry, value);
	}
}

static sly_value
dictionary_entry_ref_by_hash(sly_value d, u64 h)
{
	size_t idx = dict_get_slot(d, h);
	vector *dict = GET_PTR(d);
	return dict->elems[idx];
}

sly_value
dictionary_entry_ref(sly_value d, sly_value key)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	sly_assert(!null_p(key), "Type Error key cannot be <null>");
	sly_assert(!void_p(key), "Type Error key cannot be <void>");
	u64 h = sly_hash(key);
	return dictionary_entry_ref_by_hash(d, h);
}

sly_value
dictionary_ref(sly_value d, sly_value key)
{
	sly_value entry = dictionary_entry_ref(d, key);
	sly_assert(!slot_is_free(entry), "Dictionary Key Error key not found");
	return cdr(dictionary_entry_ref(d, key));
}

void
dictionary_remove(sly_value d, sly_value key)
{
	sly_assert(dictionary_p(d), "Type Error expected <dictionary>");
	sly_assert(!null_p(key), "Type Error key cannot be <null>");
	sly_assert(!void_p(key), "Type Error key cannot be <void>");
	u64 h = sly_hash(key);
	size_t idx = dict_get_slot(d, h);
	vector *dict = GET_PTR(d);
	sly_assert(!slot_is_free(dict->elems[idx]), "Dictionary Key Error key not found");
	set_car(dict->elems[idx], SLY_NULL); // null key indicates slot is free
	dict->len--;
}
