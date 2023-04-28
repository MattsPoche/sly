#ifndef SLY_BUILTINS_H_
#define SLY_BUILTINS_H_

/* Builtin function library.
 * Include once in sly_compile.c
 */

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
clist_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(null_p(cdr(tail(vector_ref(args, 0)))));
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
cdictionary_p(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return ctobool(dictionary_p(vector_ref(args, 0)));
}

static sly_value
cstring_to_symbol(Sly_State *ss, sly_value args)
{
	sly_value arg = vector_ref(args, 0);
	if (!string_p(arg)) {
		sly_raise_exception(ss, EXC_TYPE, "Type Error expected symbol");
	}
	byte_vector *bv = GET_PTR(arg);
	return make_symbol(ss, (char *)bv->elems, bv->len);
}

static sly_value
csymbol_to_string(Sly_State *ss, sly_value args)
{
	sly_value arg = vector_ref(args, 0);
	if (!symbol_p(arg)) {
		sly_raise_exception(ss, EXC_TYPE, "Type Error expected symbol");
	}
	symbol *sym = GET_PTR(arg);
	return make_string(ss, (char *)sym->name, sym->len);
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
	UNUSED(ss);
	return car(vector_ref(args, 0));
}

static sly_value
ccdr(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return cdr(vector_ref(args, 0));
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
	UNUSED(args);
	return gensym(ss);
}

static sly_value
cmake_syntax(Sly_State *ss, sly_value args)
{
	return make_syntax(ss, (token){0}, vector_ref(args, 0));
}

static sly_value
csyntax_to_datum(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	return syntax_to_datum(vector_ref(args, 0));
}

static sly_value
cmake_vector(Sly_State *ss, sly_value args)
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
cdictionary_ref(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	sly_value dict = vector_ref(args, 0);
	sly_value key = vector_ref(args, 1);
	return dictionary_ref(dict, key);
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
cvoid(Sly_State *ss, sly_value args)
{
	UNUSED(ss);
	UNUSED(args);
	return SLY_VOID;
}

static void
init_builtins(Sly_State *ss)
{
	struct compile *cc = ss->cc;
	sly_value symtable = cc->cscope->symtable;
	union symbol_properties st_prop = {0};
	sly_value sym;
	st_prop.p.islocal = 0;
	st_prop.p.type = sym_global;
	cc->globals = make_dictionary(ss);
	ADD_BUILTIN("+", cadd, 0, 1);
	ADD_BUILTIN("-", csub, 0, 1);
	ADD_BUILTIN("*", cmul, 0, 1);
	ADD_BUILTIN("/", cdiv, 0, 1);
	ADD_BUILTIN("%", cmod, 2, 1);
	ADD_BUILTIN("=", cnum_eq, 2, 0);
	ADD_BUILTIN("<", cnum_lt, 2, 0);
	ADD_BUILTIN(">", cnum_gt, 2, 0);
	ADD_BUILTIN("<=", cnum_leq, 2, 0);
	ADD_BUILTIN(">=", cnum_geq, 2, 0);
	ADD_BUILTIN("/=", cnum_noteq, 2, 0);
	ADD_BUILTIN("not", cnot, 1, 0);
	ADD_BUILTIN("null?", cnull_p, 1, 0);
	ADD_BUILTIN("pair?", cpair_p, 1, 0);
	ADD_BUILTIN("list?", clist_p, 1, 0);
	ADD_BUILTIN("boolean?", cboolean_p, 1, 0);
	ADD_BUILTIN("number?", cnumber_p, 1, 0);
	ADD_BUILTIN("integer?", cinteger_p, 1, 0);
	ADD_BUILTIN("real?", creal_p, 1, 0);
	ADD_BUILTIN("char?", cchar_p, 1, 0);
	ADD_BUILTIN("symbol?", csymbol_p, 1, 0);
	ADD_BUILTIN("vector?", cvector_p, 1, 0);
	ADD_BUILTIN("byte-vector?", cbyte_vector_p, 1, 0);
	ADD_BUILTIN("string?", cstring_p, 1, 0);
	ADD_BUILTIN("dictionary?", cdictionary_p, 1, 0);
	ADD_BUILTIN("procedure?", cprocedure_p, 1, 0);
	ADD_BUILTIN("syntax?", csyntax_p, 1, 0);
	ADD_BUILTIN("syntax-pair?", csyntax_pair_p, 1, 0);
	ADD_BUILTIN("identifier?", cidentifier_p, 1, 0);
	ADD_BUILTIN("equal?", cequal_p, 2, 0);
	ADD_BUILTIN("cons", ccons, 2, 0);
	ADD_BUILTIN("car", ccar, 1, 0);
	ADD_BUILTIN("cdr", ccdr, 1, 0);
	ADD_BUILTIN("set-car!", cset_car, 2, 0);
	ADD_BUILTIN("set-cdr!", cset_cdr, 2, 0);
	ADD_BUILTIN("display", cdisplay, 1, 0);
	ADD_BUILTIN("gensym", cgensym, 0, 0);
	ADD_BUILTIN("void", cvoid, 0, 0);
	ADD_BUILTIN("string->symbol", cstring_to_symbol, 1, 0);
	ADD_BUILTIN("symbol->string", csymbol_to_string, 1, 0);
	ADD_BUILTIN("make-syntax", cmake_syntax, 1, 0);
	ADD_BUILTIN("syntax->datum", csyntax_to_datum, 1, 0);
	ADD_BUILTIN("make-vector", cmake_vector, 0, 1);
	ADD_BUILTIN("vector-ref", cvector_ref, 2, 0);
	ADD_BUILTIN("vector-set!", cvector_set, 3, 0);
	ADD_BUILTIN("vector-length", cvector_length, 1, 0);
	ADD_BUILTIN("make-byte-vector", cmake_byte_vector, 0, 1);
	ADD_BUILTIN("byte-vector-ref", cbyte_vector_ref, 2, 0);
	ADD_BUILTIN("byte-vector-set!", cbyte_vector_set, 3, 0);
	ADD_BUILTIN("byte-vector-length", cbyte_vector_length, 1, 0);
	ADD_BUILTIN("vector-length", cvector_length, 1, 0);
	ADD_BUILTIN("make-dictionary", cmake_dictionary, 0, 1);
	ADD_BUILTIN("dictionary-ref", cdictionary_ref, 2, 0);
	ADD_BUILTIN("dictionary-set!", cdictionary_set, 3, 0);
}

#endif /* SLY_BUILTINS_H_ */