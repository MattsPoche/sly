#ifndef SLY_BUILTINS_H_
#define SLY_BUILTINS_H_

#include "sly_ports.h"

/* Builtin function library.
 * Include once in sly_compile.c
 */

#define ADD_BUILTIN(name, fn, nargs, has_vargs)							\
	do {																\
		sym = make_symbol(ss, name, strlen(name));						\
		dictionary_set(ss, symtable, sym, st_prop.v);					\
		sly_value entry = cons(ss, sym,									\
							   make_cclosure(ss, fn, nargs, has_vargs)); \
		dictionary_set(ss, cc->globals, car(entry), cdr(entry));		\
		cc->builtins = cons(ss, entry, cc->builtins);					\
	} while (0)
#define ADD_VARIABLE(name, value_expr)					\
	do {												\
		sym = make_symbol(ss, name, strlen(name));		\
		dictionary_set(ss, symtable, sym, st_prop.v);	\
		sly_value value = value_expr;					\
		sly_value entry = cons(ss, sym, value);			\
		dictionary_set(ss, cc->globals, sym, value);	\
		cc->builtins = cons(ss, entry, cc->builtins);	\
	} while (0)


static sly_value
cadd(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 0);
	while (!null_p(vargs)) {
		total = sly_add(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

static sly_value
csub(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 0);
	if (null_p(vargs)) {
		return total;
	}
	sly_value head = car(vargs);
	vargs = cdr(vargs);
	if (null_p(vargs)) {
		return sly_sub(ss, total, head);
	}
	while (!null_p(vargs)) {
		total = sly_add(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return sly_sub(ss, head, total);
}

static sly_value
cmul(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 1);
	while (!null_p(vargs)) {
		total = sly_mul(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

static sly_value
cdiv(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	sly_value total = make_int(ss, 1);
	if (null_p(vargs)) {
		sly_raise_exception(ss, EXC_GENERIC, "Divide by zero");
	}
	sly_value head = car(vargs);
	vargs = cdr(vargs);
	if (null_p(vargs)) {
		return sly_sub(ss, total, head);
	}
	while (!null_p(vargs)) {
		total = sly_mul(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return sly_div(ss, head, total);
}

static sly_value
cfloor_div(Sly_State *ss, sly_value args)
{
	sly_value x = vector_ref(args, 0);
	sly_value y = vector_ref(args, 1);
	return sly_floor_div(ss, x, y);
}

static sly_value
cmod(Sly_State *ss, sly_value args)
{
	sly_value total = sly_mod(ss, vector_ref(args, 0), vector_ref(args, 1));
	sly_value vargs = vector_ref(args, 2);
	while (!null_p(vargs)) {
		total = sly_mod(ss, total, car(vargs));
		vargs = cdr(vargs);
	}
	return total;
}

static sly_value
cbitwise_and(Sly_State *ss, sly_value args)
{
	return sly_bitwise_and(ss,
						   vector_ref(args, 0),
						   vector_ref(args, 1));
}

static sly_value
cbitwise_ior(Sly_State *ss, sly_value args)
{
	return sly_bitwise_ior(ss,
						   vector_ref(args, 0),
						   vector_ref(args, 1));
}

static sly_value
cbitwise_xor(Sly_State *ss, sly_value args)
{
	return sly_bitwise_xor(ss,
						   vector_ref(args, 0),
						   vector_ref(args, 1));
}

static sly_value
cbitwise_eqv(Sly_State *ss, sly_value args)
{
	return sly_bitwise_eqv(ss,
						   vector_ref(args, 0),
						   vector_ref(args, 1));
}

static sly_value
cbitwise_nand(Sly_State *ss, sly_value args)
{
	return sly_bitwise_nand(ss,
							vector_ref(args, 0),
							vector_ref(args, 1));
}

static sly_value
cbitwise_nor(Sly_State *ss, sly_value args)
{
	return sly_bitwise_nor(ss,
						   vector_ref(args, 0),
						   vector_ref(args, 1));
}

static sly_value
cbitwise_not(Sly_State *ss, sly_value args)
{
	return sly_bitwise_not(ss, vector_ref(args, 0));
}

static sly_value
carithmetic_shift(Sly_State *ss, sly_value args)
{
	return sly_arithmetic_shift(ss,
								vector_ref(args, 0),
								vector_ref(args, 1));
}

static sly_value
cnum_eq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

static sly_value
cnum_lt(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_lt(vector_ref(args, 0), vector_ref(args, 1)));
}

static sly_value
cnum_gt(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_gt(vector_ref(args, 0), vector_ref(args, 1)));
}

static sly_value
cnum_leq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_lt(vector_ref(args, 0), vector_ref(args, 1))
				   || sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

static sly_value
cnum_geq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(sly_num_gt(vector_ref(args, 0), vector_ref(args, 1))
				   || sly_num_eq(vector_ref(args, 0), vector_ref(args, 1)));
}

static sly_value
cint_to_char(Sly_State *ss, sly_value args)
{
	sly_value i = vector_ref(args, 0);
	return make_byte(ss, get_int(i));
}

static sly_value
cchar_to_int(Sly_State *ss, sly_value args)
{
	sly_value i = vector_ref(args, 0);
	return make_int(ss, get_byte(i));
}

static sly_value
cnot(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	if (vector_ref(args, 0) == SLY_FALSE) {
		return SLY_TRUE;
	} else {
		return SLY_FALSE;
	}
}

static sly_value
cnull_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(null_p(vector_ref(args, 0)));
}

static sly_value
cpair_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(pair_p(vector_ref(args, 0)));
}

static sly_value
clist_to_vector(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	return list_to_vector(ss, list);
}

static sly_value
cboolean_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(bool_p(vector_ref(args, 0)));
}

static sly_value
cnumber_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(number_p(vector_ref(args, 0)));
}

static sly_value
cinteger_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(int_p(vector_ref(args, 0)));
}

static sly_value
creal_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(float_p(vector_ref(args, 0)));
}

static sly_value
cchar_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	UNUSED(args);
	return SLY_FALSE;
}

static sly_value
csymbol_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(symbol_p(vector_ref(args, 0)));
}

static sly_value
cvector_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(vector_p(vector_ref(args, 0)));
}

static sly_value
cbyte_vector_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(byte_vector_p(vector_ref(args, 0)));
}

static sly_value
cstring_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(string_p(vector_ref(args, 0)));
}

static sly_value
cstring_eq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value s1 = vector_ref(args, 0);
	sly_value s2 = vector_ref(args, 1);
	sly_value rest = vector_ref(args, 2);
	if (!string_eq(s1, s2)) {
		return SLY_FALSE;
	}
	while (!null_p(rest)) {
		if (!string_eq(s1, car(rest))) {
			return SLY_FALSE;
		}
		rest = cdr(rest);
	}
	return SLY_TRUE;
}

static sly_value
cprocedure_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v = vector_ref(args, 0);
	return ctobool(closure_p(v) || cclosure_p(v) || continuation_p(v));
}

static sly_value
csyntax_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(syntax_p(vector_ref(args, 0)));
}

static sly_value
csyntax_pair_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(syntax_pair_p(vector_ref(args, 0)));
}

static sly_value
cidentifier_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(identifier_p(vector_ref(args, 0)));
}

static sly_value
cidentifier_eq(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value id0 = vector_ref(args, 0);
	sly_value id1 = vector_ref(args, 1);
	return ctobool(identifier_eq(id0, id1));
}

static sly_value
cdictionary_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(dictionary_p(vector_ref(args, 0)));
}

static sly_value
cmake_string(Sly_State *ss, sly_value args)
{
	size_t len = get_int(vector_ref(args, 0));
	sly_value str = make_uninitialized_string(ss, len);
	sly_value ch = vector_ref(args, 1);
	if (!null_p(ch)) {
		ch = car(ch);
		for (size_t i = 0; i < len; ++i) {
			string_set(str, i, ch);
		}
	}
	return str;
}

static sly_value
cstring_length(Sly_State *ss, sly_value args)
{
	sly_value s = vector_ref(args, 0);
	return make_int(ss, string_len(s));
}


static sly_value
cstring_ref(Sly_State *ss, sly_value args)
{
	sly_value str = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	return string_ref(ss, str, get_int(idx));
}

static sly_value
cstring_set(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value str = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	sly_value val = vector_ref(args, 2);
	string_set(str, get_int(idx), val);
	return val;
}

static sly_value
cstring_join(Sly_State *ss, sly_value args)
{
	sly_value ls = vector_ref(args, 0);
	sly_value delim = vector_ref(args, 1);
	if (!null_p(delim)) {
		delim = car(delim);
	} else {
		delim = make_string(ss, " ", 1);
	}
	return string_join(ss, ls, delim);
}

static sly_value
cstring_to_symbol(Sly_State *ss, sly_value args)
{
	sly_value arg = vector_ref(args, 0);
	if (!string_p(arg)) {
		sly_raise_exception(ss, EXC_TYPE, "Type Error expected string");
	}
	byte_vector *bv = GET_PTR(arg);
	return make_symbol(ss, (char *)bv->elems, bv->len);
}

static sly_value
cstring_to_number(Sly_State *ss, sly_value args)
{
	sly_value str = vector_ref(args, 0);
	sly_assert(string_p(str), "Type Error expected string");
	byte_vector *ptr = GET_PTR(str);
	for (size_t i = 0; i < ptr->len; ++i) {
		if (ptr->elems[i] == '.') {
			return make_float(ss, strtod((char *)ptr->elems, NULL));
		}
	}
	return make_int(ss, strtol((char *)ptr->elems, NULL, 0));
}

static sly_value
csymbol_to_string(Sly_State *ss, sly_value args)
{
	sly_value arg = vector_ref(args, 0);
	if (!symbol_p(arg)) {
		sly_displayln(arg);
		sly_raise_exception(ss, EXC_TYPE, "Type Error expected symbol");
	}
	symbol *sym = GET_PTR(arg);
	return make_string(ss, (char *)sym->name, sym->len);
}

static sly_value
ceq_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v1 = vector_ref(args, 0);
	sly_value v2 = vector_ref(args, 1);
	return ctobool(sly_eq(v1, v2));
}

static sly_value
ceqv_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v1 = vector_ref(args, 0);
	sly_value v2 = vector_ref(args, 1);
	return ctobool(sly_eqv(v1, v2));
}

static sly_value
cequal_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v1 = vector_ref(args, 0);
	sly_value v2 = vector_ref(args, 1);
	return ctobool(sly_equal(v1, v2));
}

static sly_value
cnum_noteq(Sly_State *ss, sly_value args)
{
	vector_set(args, 0, cnum_eq(ss, args));
	return cnot(ss, args);
}

static sly_value
ccons(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return cons(ss, vector_ref(args, 0), vector_ref(args, 1));
}

static sly_value
ccar(Sly_State *ss, sly_value args)
{
	sly_value p = vector_ref(args, 0);
	if (!pair_p(p)) {
		printf("Error (ccar) not a pair\n");
		sly_displayln(p);
		vm_bt(ss->frame);
	}
	return car(p);
}

static sly_value
ccdr(Sly_State *ss, sly_value args)
{
	sly_value p = vector_ref(args, 0);
	if (!pair_p(p)) {
		printf("Error (ccdr) not a pair\n");
		vm_bt(ss->frame);
	}
	return cdr(p);
}

static sly_value
cset_car(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v = vector_ref(args, 1);
	set_car(vector_ref(args, 0), v);
	return v;
}

static sly_value
cset_cdr(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v = vector_ref(args, 1);
	set_cdr(vector_ref(args, 0), v);
	return v;
}

static sly_value
cdisplay(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_display(vector_ref(args, 0), 0);
	return SLY_VOID;
}

static sly_value
cgensym(Sly_State *ss, sly_value args)
{
	sly_value vargs = vector_ref(args, 0);
	if (null_p(vargs)) {
		return gensym_from_cstr(ss, "g");
	}
	sly_value base = car(vargs);
	return gensym(ss, base);
}

static sly_value
csyntax_to_datum(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return strip_syntax(vector_ref(args, 0));
}

static sly_value
csyntax_to_list(Sly_State *ss, sly_value args)
{
	sly_value s = vector_ref(args, 0);
	return syntax_to_list(ss, s);
}

static sly_value
cdatum_to_syntax(Sly_State *ss, sly_value args)
{
	sly_value id = vector_ref(args, 0);
	sly_value stx = vector_ref(args, 1);
	stx = datum_to_syntax(ss, id, stx);
	syntax *_stx = GET_PTR(stx);
	syntax *_id  = GET_PTR(id);
	_stx->context &= _id->context & ctx_tail_pos;
	return stx;
}

static sly_value
craw_syntax(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value stx = vector_ref(args, 0);
	sly_assert(syntax_p(stx), "Type Error expected syntax");
	syntax *s1 = GET_PTR(stx);
	syntax *s2 = GC_MALLOC(sizeof(*s2));
	s2->type = tt_syntax;
	s2->tok = s1->tok;
	s2->datum = SLY_VOID;
	s2->context = s2->context;
	return (sly_value)s2;
}

static sly_value
csyntax(Sly_State *ss, sly_value args)
{
	sly_value datum = vector_ref(args, 0);
	sly_value scope_set = vector_ref(args, 1);
	sly_value stx = make_syntax(ss, (token){0}, datum);
	syntax *s = GET_PTR(stx);
	s->scope_set = scope_set;
	return stx;
}

static sly_value
csyntax_scopes(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value stx = vector_ref(args, 0);
	sly_assert(syntax_p(stx), "Type Error expected syntax");
	syntax *s = GET_PTR(stx);
	return s->scope_set;
}

static sly_value
cvector(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	size_t len = list_len(list);
	sly_value vec = make_vector(ss, len, len);
	for (size_t i = 0; i < len; ++i) {
		vector_set(vec, i, car(list));
		list = cdr(list);
	}
	return vec;
}

static sly_value
cmake_vector(Sly_State *ss, sly_value args)
{
	size_t len = get_int(vector_ref(args, 0));
	sly_value rest = vector_ref(args, 1);
	sly_value val = make_int(ss, 0);
	if (!null_p(rest)) {
		val = car(rest);
	}
	sly_value vec = make_vector(ss, len, len);
	for (size_t i = 0; i < len; ++i) {
		vector_set(vec, i, val);
	}
	return vec;
}

static sly_value
cvector_ref(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value vec = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	return vector_ref(vec, get_int(idx));
}

static sly_value
cvector_set(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value vec = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	sly_value val = vector_ref(args, 2);
	vector_set(vec, get_int(idx), val);
	return val;
}

static sly_value
cvector_length(Sly_State *ss, sly_value args)
{
	sly_value vec = vector_ref(args, 0);
	return make_int(ss, vector_len(vec));
}

static sly_value
cvector_to_list(Sly_State *ss, sly_value args)
{
	sly_value vec = vector_ref(args, 0);
	return vector_to_list(ss, vec);
}

static sly_value
cmake_byte_vector(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	size_t len = list_len(list);
	sly_value vec = make_byte_vector(ss, len, len);
	for (size_t i = 0; i < len; ++i) {
		byte_vector_set(vec, i, car(list));
		list = cdr(list);
	}
	return vec;
}

static sly_value
cbyte_vector_ref(Sly_State *ss, sly_value args)
{
	sly_value vec = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	return byte_vector_ref(ss, vec, get_int(idx));
}

static sly_value
cbyte_vector_set(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value vec = vector_ref(args, 0);
	sly_value idx = vector_ref(args, 1);
	sly_value val = vector_ref(args, 2);
	byte_vector_set(vec, get_int(idx), val);
	return val;
}

static sly_value
cbyte_vector_length(Sly_State *ss, sly_value args)
{
	sly_value vec = vector_ref(args, 0);
	return make_int(ss, byte_vector_len(vec));
}

static sly_value
cmake_dictionary(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	sly_value dict = make_dictionary(ss);
	while (!null_p(list)) {
		sly_value entry = car(list);
		sly_value key = car(entry);
		sly_value val = cdr(entry);
		dictionary_set(ss, dict, key, val);
		list = cdr(list);
	}
	return dict;
}

static sly_value
cdictionary_to_alist(Sly_State *ss, sly_value args)
{
	return dictionary_to_alist(ss, vector_ref(args, 0));
}

static sly_value
cdictionary_ref(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value dict = vector_ref(args, 0);
	sly_value key = vector_ref(args, 1);
	sly_value vargs = vector_ref(args, 2);
	sly_value dv = SLY_NULL;
	if (!null_p(vargs)) {
		dv = car(vargs);
	}
	sly_value entry = dictionary_entry_ref(dict, key);
	if (slot_is_free(entry)) {
		return dv;
	}
	return cdr(entry);
}

static sly_value
cdictionary_set(Sly_State *ss, sly_value args)
{
	sly_value dict = vector_ref(args, 0);
	sly_value key = vector_ref(args, 1);
	sly_value val = vector_ref(args, 2);
	dictionary_set(ss, dict, key, val);
	return val;
}

static sly_value
cdictionary_length(Sly_State *ss, sly_value args)
{
	sly_value dict = vector_ref(args, 0);
	sly_assert(dictionary_p(dict), "Type Error expected dictionary");
	vector *v = GET_PTR(dict);
	return make_int(ss, v->len);
}


static sly_value
cdictionary_has_key(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value dict = vector_ref(args, 0);
	sly_value key = vector_ref(args, 1);
	if (slot_is_free(dictionary_entry_ref(dict, key))) {
		return SLY_FALSE;
	} else {
		return SLY_TRUE;
	}
}

static sly_value
cvoid(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	UNUSED(args);
	return SLY_VOID;
}

static sly_value
clist(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return vector_ref(args, 0);
}

#if 0
static sly_value
capply(Sly_State *ss, sly_value args)
{
	sly_value fn = vector_ref(args, 0);
	args = vector_ref(args, 1);
	sly_value regs = make_vector(ss, 0, 8);
	vector_append(ss, regs, fn);
	while (!null_p(cdr(args))) {
		vector_append(ss, regs, car(args));
		args = cdr(args);
	}
	sly_value last = car(args);
	sly_assert(pair_p(last) || null_p(last),
			   "Type Error the last argument of apply must be a list");
	while (!null_p(last)) {
		vector_append(ss, regs, car(last));
		last = cdr(last);
	}
	return call_closure(ss, regs);
}
#endif

static sly_value
cclear_screen(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	UNUSED(args);
	printf("\033[2J\033[H");
	return SLY_VOID;
}

static sly_value
craise_macro_exception(Sly_State *ss, sly_value args)
{
	sly_value str = vector_ref(args, 0);
	byte_vector *bv = GET_PTR(str);
	char *msg = GC_MALLOC(bv->len+1);
	memcpy(msg, bv->elems, bv->len);
	msg[bv->len] = '\0';
	sly_raise_exception(ss, EXC_MACRO, msg);
	return SLY_VOID;
}

static sly_value
cerror(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	while (!null_p(list)) {
		sly_display(car(list), 0);
		printf(" ");
		list = cdr(list);
	}
	printf("\n");
	sly_raise_exception(ss, EXC_GENERIC, "Error signalled");
	return SLY_VOID;
}

static sly_value
csyntax_source_info(Sly_State *ss, sly_value args)
{
	sly_value stx = vector_ref(args, 0);
	sly_assert(syntax_p(stx), "Type Error expected syntax");
	syntax *s = GET_PTR(stx);
	char buff[124] = {0};
	size_t len = snprintf(buff, sizeof(buff), "%d:%d", s->tok.ln, s->tok.cn);
	return make_string(ss, buff, len);
}

static sly_value
ceval(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value ast = vector_ref(args, 0);
	prototype *proto = GET_PTR(ss->cc->cscope->proto);
	proto->code = make_vector(ss, 0, 16);
	sly_value clos = sly_compile(ss, ast);
	sly_value rval = eval_closure(ss, clos, SLY_NULL);
	return rval;
}

static sly_value
cread(Sly_State *ss, sly_value args)
{
	char *cstr = string_to_cstr(vector_ref(args, 0));
	sly_value ast = parse(ss, cstr);
	return ast;
}

static sly_value
cread_file(Sly_State *ss, sly_value args)
{
	char *cstr = string_to_cstr(vector_ref(args, 0));
	char *contents;
	sly_value ast = parse_file(ss, cstr, &contents);
	printf("%s\n", cstr);
	return ast;
}

static sly_value
cbuiltins(Sly_State *ss, sly_value args)
{
	UNUSED(args);
	return ss->cc->builtins;
}

static sly_value
cvargs(Sly_State *ss, sly_value args)
{
	UNUSED(args);
	return dictionary_ref(ss->cc->globals, cstr_to_symbol("__VARGS__"));
}

static sly_value
cmatch_syntax(Sly_State *ss, sly_value args)
{
	sly_value pattern = syntax_to_list(ss, vector_ref(args, 0));
	sly_value literals = vector_ref(args, 1);
	sly_value form = syntax_to_list(ss, vector_ref(args, 2));
	sly_value pvars = vector_ref(args, 3);
	return ctobool(match_syntax(ss, pattern, literals, form, pvars, 0));
}

static sly_value
cget_pattern_var_names(Sly_State *ss, sly_value args)
{
	sly_value pattern = syntax_to_list(ss, vector_ref(args, 0));
	sly_value literals = vector_ref(args, 1);
	return get_pattern_var_names(ss, pattern, literals);
}

static sly_value
cconstruct_syntax(Sly_State *ss, sly_value args)
{
	sly_value template = vector_ref(args, 0);
	sly_value pvars = vector_ref(args, 1);
	sly_value names = vector_ref(args, 2);
	sly_value slist = construct_syntax(ss, syntax_to_list(ss, template), pvars, names, 0);
	return datum_to_syntax(ss, template, slist);
}

static sly_value
cdis_dis(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value v = vector_ref(args, 0);
	sly_value v2 = vector_ref(args, 1);
	int b = 0;
	if (!null_p(v2)) {
		b = booltoc(car(v2));
	}
	sly_value proto;
	if (continuation_p(v)) {
		continuation *c = GET_PTR(v);
		proto = get_prototype(c->frame->clos);
	} else {
		proto = get_prototype(v);
	}
	if (b) {
		dis_prototype_rec(proto, 1);
	} else {
		dis_prototype(proto, 1);
	}
	return SLY_VOID;
}

static sly_value
cfile_readable(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value s = vector_ref(args, 0);
	int die = booltoc(vector_ref(args, 1));
	sly_assert(string_p(s), "Type Error expected <string>");
	char *file_path = string_to_cstr(s);
	if (access(file_path, R_OK) < 0) {
		if (die) {
			printf("%s\n", strerror(errno));
			sly_assert(0, "File Access Error");
		} else {
			return SLY_FALSE;
		}
	}
	return SLY_TRUE;
}

static sly_value
cinput_port_p(Sly_State *ss, sly_value args)
{
	return ctobool(input_port_p(ss, vector_ref(args, 0)));
}

static sly_value
coutput_port_p(Sly_State *ss, sly_value args)
{
	return ctobool(output_port_p(ss, vector_ref(args, 0)));
}

static sly_value
ceof_object_p(Sly_State *ss, sly_value args)
{
	return ctobool(eof_object_p(ss, vector_ref(args, 0)));
}

static sly_value
cstring_port_p(Sly_State *ss, sly_value args)
{
	return ctobool(string_port_p(ss, vector_ref(args, 0)));
}

static sly_value
cport_p(Sly_State *ss, sly_value args)
{
	return ctobool(port_p(ss, vector_ref(args, 0)));
}

static sly_value
cport_closed_p(Sly_State *ss, sly_value args)
{
	return ctobool(port_closed_p(ss, vector_ref(args, 0)));
}

static sly_value
cfile_stream_port_p(Sly_State *ss, sly_value args)
{
	return ctobool(file_stream_port_p(ss, vector_ref(args, 0)));
}

static sly_value
copen_input_file(Sly_State *ss, sly_value args)
{
	return open_input_file(ss, vector_ref(args, 0));
}

static sly_value
copen_output_file(Sly_State *ss, sly_value args)
{
	return open_output_file(ss, vector_ref(args, 0), 0);
}

static sly_value
cclose_input_port(Sly_State *ss, sly_value args)
{
	close_input_port(ss, vector_ref(args, 0));
	return SLY_VOID;
}

static sly_value
cclose_output_port(Sly_State *ss, sly_value args)
{
	close_output_port(ss, vector_ref(args, 0));
	return SLY_VOID;
}

static sly_value
copen_output_string(Sly_State *ss, sly_value args)
{
	UNUSED(args);
	open_output_string(ss);
	return SLY_VOID;
}

static sly_value
cget_output_string(Sly_State *ss, sly_value args)
{
	return get_output_string(ss, vector_ref(args, 0));
}

static sly_value
cflush_output(Sly_State *ss, sly_value args)
{
	flush_output(ss, vector_ref(args, 0));
	return SLY_VOID;
}

static sly_value
cfile_position(Sly_State *ss, sly_value args)
{
	sly_value port = vector_ref(args, 0);
	sly_value v = vector_ref(args, 1);
	i64 pos = -1;
	if (!null_p(v) && int_p(v)) {
		pos = get_int(v);
	}
	return make_int(ss, file_position(ss, port, pos));
}

static sly_value
ccurrent_input_port(Sly_State *ss, sly_value args)
{
	UNUSED(args);
	return dictionary_ref(ss->cc->globals, cstr_to_symbol("*STDIN*"));
}

static sly_value
ccurrent_output_port(Sly_State *ss, sly_value args)
{
	UNUSED(args);
	return dictionary_ref(ss->cc->globals, cstr_to_symbol("*STDOUT*"));
}

static sly_value
ccurrent_error_port(Sly_State *ss, sly_value args)
{
	UNUSED(args);
	return dictionary_ref(ss->cc->globals, cstr_to_symbol("*STDERR*"));
}

static sly_value
cwrite_char(Sly_State *ss, sly_value args)
{
	sly_value ch = vector_ref(args, 0);
	sly_value list = vector_ref(args, 1);
	sly_value port;
	if (null_p(list)) {
		port = ccurrent_output_port(ss, SLY_NULL);
	} else {
		port = car(list);
	}
	write_char(ss, ch, port);
	return SLY_VOID;
}

static sly_value
cwrite_string(Sly_State *ss, sly_value args)
{
	sly_value str = vector_ref(args, 0);
	sly_value list = vector_ref(args, 1);
	sly_value port = ccurrent_output_port(ss, SLY_NULL);
	i64 start = 0;
	i64 end = string_len(str);
	if (!null_p(list)) {
		port = car(list);
		list = cdr(list);
		if (!null_p(list)) {
			start = get_int(car(list));
			list = cdr(list);
			if (!null_p(list)) {
				end = get_int(car(list));
			}
		}
	}
	return make_int(ss, write_string(ss, str, port, start, end));
}

static sly_value
cread_char(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	sly_value port;
	if (null_p(list)) {
		port = ccurrent_input_port(ss, SLY_NULL);
	} else {
		port = car(list);
	}
	return read_char(ss, port);
}

static sly_value
cread_line(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	sly_value port;
	if (null_p(list)) {
		port = ccurrent_input_port(ss, SLY_NULL);
	} else {
		port = car(list);
	}
	return read_line(ss, port);
}

static sly_value
cport_to_string(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	sly_value port;
	if (null_p(list)) {
		port = ccurrent_input_port(ss, SLY_NULL);
	} else {
		port = car(list);
	}
	return port_to_string(ss, port);
}

static sly_value
cport_to_lines(Sly_State *ss, sly_value args)
{
	sly_value list = vector_ref(args, 0);
	sly_value port;
	if (null_p(list)) {
		port = ccurrent_input_port(ss, SLY_NULL);
	} else {
		port = car(list);
	}
	return port_to_lines(ss, port);
}

static void
init_builtins(Sly_State *ss)
{
	struct compile *cc = ss->cc;
	sly_value symtable = cc->cscope->symtable;
	union symbol_properties st_prop = {0};
	sly_value sym;
	st_prop.p.type = sym_global;
	cc->globals = make_dictionary(ss);
	cc->builtins = SLY_NULL;
	ADD_BUILTIN("+", cadd, 0, 1);
	ADD_BUILTIN("-", csub, 0, 1);
	ADD_BUILTIN("*", cmul, 0, 1);
	ADD_BUILTIN("/", cdiv, 0, 1);
	ADD_BUILTIN("div", cfloor_div, 2, 0);
	ADD_BUILTIN("bitwise-and", cbitwise_and, 2, 0);
	ADD_BUILTIN("bitwise-ior", cbitwise_ior, 2, 0);
	ADD_BUILTIN("bitwise-xor", cbitwise_xor, 2, 0);
	ADD_BUILTIN("bitwise-eqv", cbitwise_eqv, 2, 0);
	ADD_BUILTIN("bitwise-nor", cbitwise_nor, 2, 0);
	ADD_BUILTIN("bitwise-nand", cbitwise_nand, 2, 0);
	ADD_BUILTIN("bitwise-not", cbitwise_not, 1, 0);
	ADD_BUILTIN("arithmetic-shift", carithmetic_shift, 2, 0);
	ADD_BUILTIN("%", cmod, 2, 1);
	ADD_BUILTIN("=", cnum_eq, 2, 0);
	ADD_BUILTIN("<", cnum_lt, 2, 0);
	ADD_BUILTIN(">", cnum_gt, 2, 0);
	ADD_BUILTIN("<=", cnum_leq, 2, 0);
	ADD_BUILTIN(">=", cnum_geq, 2, 0);
	ADD_BUILTIN("/=", cnum_noteq, 2, 0);
	ADD_BUILTIN("not", cnot, 1, 0);
	ADD_BUILTIN("integer->char", cint_to_char, 1, 0);
	ADD_BUILTIN("char->integer", cchar_to_int, 1, 0);
	ADD_BUILTIN("null?", cnull_p, 1, 0);
	ADD_BUILTIN("pair?", cpair_p, 1, 0);
	ADD_BUILTIN("list->vector", clist_to_vector, 1, 0);
	ADD_BUILTIN("boolean?", cboolean_p, 1, 0);
	ADD_BUILTIN("number?", cnumber_p, 1, 0);
	ADD_BUILTIN("integer?", cinteger_p, 1, 0);
	ADD_BUILTIN("real?", creal_p, 1, 0);
	ADD_BUILTIN("char?", cchar_p, 1, 0);
	ADD_BUILTIN("symbol?", csymbol_p, 1, 0);
	ADD_BUILTIN("vector?", cvector_p, 1, 0);
	ADD_BUILTIN("byte-vector?", cbyte_vector_p, 1, 0);
	ADD_BUILTIN("string?", cstring_p, 1, 0);
	ADD_BUILTIN("string=?", cstring_eq, 2, 1);
	ADD_BUILTIN("dictionary?", cdictionary_p, 1, 0);
	ADD_BUILTIN("procedure?", cprocedure_p, 1, 0);
	ADD_BUILTIN("syntax?", csyntax_p, 1, 0);
	ADD_BUILTIN("syntax-pair?", csyntax_pair_p, 1, 0);
	ADD_BUILTIN("identifier?", cidentifier_p, 1, 0);
	ADD_BUILTIN("identifier=?", cidentifier_eq, 2, 0);
	ADD_BUILTIN("eq?", ceq_p, 2, 0);
	ADD_BUILTIN("eqv?", ceqv_p, 2, 0);
	ADD_BUILTIN("equal?", cequal_p, 2, 0);
	ADD_BUILTIN("cons", ccons, 2, 0);
	ADD_BUILTIN("car", ccar, 1, 0);
	ADD_BUILTIN("cdr", ccdr, 1, 0);
	ADD_BUILTIN("set-car!", cset_car, 2, 0);
	ADD_BUILTIN("set-cdr!", cset_cdr, 2, 0);
	ADD_BUILTIN("display", cdisplay, 1, 0);
	ADD_BUILTIN("gensym", cgensym, 0, 1);
	ADD_BUILTIN("void", cvoid, 0, 0);
	ADD_BUILTIN("make-string", cmake_string, 1, 1);
	ADD_BUILTIN("string-length", cstring_length, 1, 0);
	ADD_BUILTIN("string-ref", cstring_ref, 2, 0);
	ADD_BUILTIN("string-set!", cstring_set, 3, 0);
	ADD_BUILTIN("string-join", cstring_join, 1, 1);
	ADD_BUILTIN("string->symbol", cstring_to_symbol, 1, 0);
	ADD_BUILTIN("string->number", cstring_to_number, 1, 0);
	ADD_BUILTIN("symbol->string", csymbol_to_string, 1, 0);
	ADD_BUILTIN("syntax->datum", csyntax_to_datum, 1, 0);
	ADD_BUILTIN("syntax->list", csyntax_to_list, 1, 0);
	ADD_BUILTIN("datum->syntax", cdatum_to_syntax, 2, 0);
	ADD_BUILTIN("raw-syntax", craw_syntax, 1, 0);
	ADD_BUILTIN("syntax", csyntax, 2, 0);
	ADD_BUILTIN("syntax-scopes", csyntax_scopes, 1, 0);
	ADD_BUILTIN("make-vector", cmake_vector, 1, 1);
	ADD_BUILTIN("vector", cvector, 0, 1);
	ADD_BUILTIN("vector-ref", cvector_ref, 2, 0);
	ADD_BUILTIN("vector-set!", cvector_set, 3, 0);
	ADD_BUILTIN("vector-length", cvector_length, 1, 0);
	ADD_BUILTIN("vector->list", cvector_to_list, 1, 0);
	ADD_BUILTIN("make-byte-vector", cmake_byte_vector, 0, 1);
	ADD_BUILTIN("byte-vector-ref", cbyte_vector_ref, 2, 0);
	ADD_BUILTIN("byte-vector-set!", cbyte_vector_set, 3, 0);
	ADD_BUILTIN("byte-vector-length", cbyte_vector_length, 1, 0);
	ADD_BUILTIN("vector-length", cvector_length, 1, 0);
	ADD_BUILTIN("make-dictionary", cmake_dictionary, 0, 1);
	ADD_BUILTIN("dictionary->alist", cdictionary_to_alist, 1, 0);
	ADD_BUILTIN("dictionary-ref", cdictionary_ref, 2, 1);
	ADD_BUILTIN("dictionary-set!", cdictionary_set, 3, 0);
	ADD_BUILTIN("dictionary-length", cdictionary_length, 1, 0);
	ADD_BUILTIN("dictionary-has-key?", cdictionary_has_key, 2, 0);
	ADD_BUILTIN("list", clist, 0, 1);
	ADD_BUILTIN("console-clear-screen", cclear_screen, 0, 0);
	ADD_BUILTIN("raise-macro-exception", craise_macro_exception, 1, 0);
	ADD_BUILTIN("error", cerror, 0, 1);
	ADD_BUILTIN("eval", ceval, 1, 0);
	ADD_BUILTIN("read", cread, 1, 0);
	ADD_BUILTIN("read-file", cread_file, 1, 0);
	ADD_BUILTIN("builtins", cbuiltins, 0, 0);
	ADD_BUILTIN("get-vargs", cvargs, 0, 0);
	ADD_BUILTIN("syntax-source-info", csyntax_source_info, 1, 0);
	ADD_BUILTIN("match-syntax", cmatch_syntax, 4, 0);
	ADD_BUILTIN("get-pattern-var-names", cget_pattern_var_names, 2, 0);
	ADD_BUILTIN("construct-syntax", cconstruct_syntax, 3, 0);
	ADD_BUILTIN("disassemble", cdis_dis, 1, 1);
	ADD_BUILTIN("input-port?", cinput_port_p, 1, 0);
	ADD_BUILTIN("output-input-port?", coutput_port_p, 1, 0);
	ADD_BUILTIN("port?", cport_p, 1, 0);
	ADD_BUILTIN("file-stream-port?", cfile_stream_port_p, 1, 0);
	ADD_BUILTIN("port-closed?", cport_closed_p, 1, 0);
	ADD_BUILTIN("open-input-file", copen_input_file, 1, 0);
	ADD_BUILTIN("open-output-file", copen_output_file, 1, 0);
	ADD_BUILTIN("close-input-port", cclose_input_port, 1, 0);
	ADD_BUILTIN("close-output-port", cclose_output_port, 1, 0);
	ADD_BUILTIN("write-char", cwrite_char, 1, 1);
	ADD_BUILTIN("write-string", cwrite_string, 1, 3);
	ADD_BUILTIN("read-char", cread_char, 0, 1);
	ADD_BUILTIN("read-line", cread_line, 0, 1);
	ADD_BUILTIN("port->string", cport_to_string, 0, 1);
	ADD_BUILTIN("port->lines", cport_to_lines, 0, 1);
	ADD_BUILTIN("current-input-port", ccurrent_input_port, 0, 0);
	ADD_BUILTIN("current-output-port", ccurrent_output_port, 0, 0);
	ADD_BUILTIN("current-error-port", ccurrent_error_port, 0, 0);
	ADD_BUILTIN("file-position", cfile_position, 1, 1);
	ADD_BUILTIN("flush-output", cflush_output, 1, 0);
	ADD_BUILTIN("get-output-string", cget_output_string, 1, 0);
	ADD_BUILTIN("open-output-string", copen_output_string, 0, 0);
	ADD_BUILTIN("string-port?", cstring_port_p, 1, 0);
	ADD_BUILTIN("eof-object?", ceof_object_p, 1, 0);
	ADD_BUILTIN("file-readable?", cfile_readable, 2, 0);
	ADD_VARIABLE("*REQUIRED*", make_dictionary(ss));
	sly_value port = make_input_port(ss);
	port_set_stream(port, stdin);
	ADD_VARIABLE("*STDIN*", port);
	port = make_output_port(ss);
	port_set_stream(port, stdout);
	ADD_VARIABLE("*STDOUT*", port);
	port = make_output_port(ss);
	port_set_stream(port, stderr);
	ADD_VARIABLE("*STDERR*", port);
	ADD_VARIABLE("eof", gensym_from_cstr(ss, "eof"));
}

#endif /* SLY_BUILTINS_H_ */
