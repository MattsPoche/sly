#include <string.h>
#include "sly_types.h"
#include "opcodes.h"
#include "sly_compile.h"
#include "syntax_expander.h"
#include "eval.h"
#include "sly_vm.h"
#include "sly_alloc.h"

/* TODO: Implement module system */

#define scope() gensym(ss)
#define add_binding(id, binding)							\
		dictionary_set(ss, all_bindings, id, binding);
#define core_symbol(cf_i) \
	vector_ref(core_forms, cf_i)

enum core_form {
	cf_define = 0,
	cf_lambda,
	cf_quote,
	cf_syntax_quote,
	cf_begin,
	cf_if,
	cf_set,
	cf_define_syntax,
	cf_call_with_current_continuation,
	cf_callwcc,
	CORE_FORM_COUNT,
};

static char *core_form_names[CORE_FORM_COUNT] = {
	[cf_define] = "define",
	[cf_lambda] = "lambda",
	[cf_quote] = "quote",
	[cf_syntax_quote] = "syntax-quote",
	[cf_begin] = "begin",
	[cf_if] = "if",
	[cf_set] = "set!",
	[cf_define_syntax] = "define-syntax",
	[cf_call_with_current_continuation] = "call-with-current-continuation",
	[cf_callwcc] = "call/cc",
};

static sly_value core_forms = SLY_NULL;
static sly_value core_scope = SLY_NULL;
static sly_value variable = SLY_NULL;
static sly_value all_bindings = SLY_NULL;

typedef sly_value (*set_op)(Sly_State *, sly_value, sly_value);

static sly_value csyntax(Sly_State *ss, sly_value datum, sly_value scope_set);
static sly_value expand(Sly_State *ss, sly_value s, sly_value env);
static sly_value expand_app(Sly_State *ss, sly_value s, sly_value env);
static sly_value expand_id_application_form(Sly_State *ss, sly_value s, sly_value env);
static sly_value expand_identifier(Sly_State *ss, sly_value s, sly_value env);
static sly_value expand_lambda(Sly_State *ss, sly_value s, sly_value env);
static sly_value expand_define(Sly_State *ss, sly_value s, sly_value env);
static sly_value expand_define_syntax(Sly_State *ss, sly_value s, sly_value env);
static sly_value expand_core_form(Sly_State *ss, sly_value s, sly_value env);
static sly_value apply_transformer(Sly_State *ss, sly_value t, sly_value s);
static sly_value compile(Sly_State *ss, sly_value s);
static sly_value compile_lambda(Sly_State *ss, sly_value s);


static sly_value
macro_call(Sly_State *ss, sly_value macro, sly_value form)
{
	sly_value args = make_vector(ss, 1, 1);
	vector_set(args, 0, form);
	if (prototype_p(macro)) {
		macro = make_closure(ss, macro);
	}
	sly_assert(closure_p(macro), "Type Error macro must be a procedure");
	int arity = sly_arity(macro);
	sly_assert(arity == 0 || arity == 1,
			   "Value Error macros may have at most one argument");
	closure *clos = GET_PTR(macro);
	vector_set(clos->upvals, 0,
			   make_closed_upvalue(ss, ss->cc->globals));
	return eval_closure(ss, macro, args, 0);
}

static int
set_contains(Sly_State *ss, sly_value set, sly_value value)
{ // check if set contains a value
	UNUSED(ss);
	return !slot_is_free(dictionary_entry_ref(set, value));
}

static int
is_subset(Sly_State *ss, sly_value set1, sly_value set2)
{ // check if set2 is a subset of set1
	vector *vec = GET_PTR(set2);
	sly_value entry;
	for (size_t i = 0; i < vec->cap; ++i) {
		entry = vec->elems[i];
		if (!slot_is_free(entry)
			&& !set_contains(ss, set1, car(entry))) {
			/* if set2 has a value not found in slot1 */
			return 0;
		}
	}
	return 1;
}

static sly_value
set_remove(Sly_State *ss, sly_value set, sly_value value)
{
	UNUSED(ss);
	dictionary_remove(set, value);
	return set;
}

static sly_value
set_add(Sly_State *ss, sly_value set, sly_value value)
{
	dictionary_set(ss, set, value, SLY_NULL);
	return set;
}

static sly_value
set_flip(Sly_State *ss, sly_value set, sly_value value)
{
	if (set_contains(ss, set, value)) {
		set_remove(ss, set, value);
	} else {
		set_add(ss, set, value);
	}
	return set;
}

static sly_value
adjust_scope(Sly_State *ss, sly_value s, sly_value sc, set_op op)
{
	if (syntax_p(s)) {
		if (null_p(syntax_scopes(s))) {
			syntax *stx = GET_PTR(s);
			stx->scope_set = make_dictionary(ss);
		}
		return csyntax(ss,
					   syntax_to_datum(s),
					   op(ss, syntax_scopes(s), sc));
	} else if (pair_p(s)) {
		return cons(ss,
					adjust_scope(ss, car(s), sc, op),
					adjust_scope(ss, cdr(s), sc, op));
	}
	return s;
}

static sly_value
add_scope(Sly_State *ss, sly_value s, sly_value sc)
{
	return adjust_scope(ss, s, sc, set_add);
}

static sly_value
flip_scope(Sly_State *ss, sly_value s, sly_value sc)
{
	return adjust_scope(ss, s, sc, set_flip);
}

static sly_value
get_max_id(sly_value ids)
{
	sly_value id, max_id = car(ids);
	vector *v_id, *v_max;
	ids = cdr(ids);
	while (!null_p(ids)) {
		id = car(ids);
		v_max = GET_PTR(syntax_scopes(max_id));
		v_id = GET_PTR(syntax_scopes(id));
		if (v_id->len > v_max->len) {
			max_id = id;
		}
		ids = cdr(ids);
	}
	return max_id;
}

static sly_value
find_all_matching_bindings(Sly_State *ss, sly_value id)
{
	vector *vec = GET_PTR(all_bindings);
	sly_value entry, c_id, matches = SLY_NULL;
	for (size_t i = 0; i < vec->cap; ++i) {
		entry = vec->elems[i];
		if (!slot_is_free(entry)) {
			c_id = car(entry);
			if (sly_equal(syntax_to_datum(id), syntax_to_datum(c_id))
				&& is_subset(ss, syntax_scopes(id), syntax_scopes(c_id))) {
				matches = cons(ss, c_id, matches);
			}
		}
	}
	return matches;
}

static void
check_unambiguous(Sly_State *ss, sly_value max_id, sly_value candidate_ids)
{
	sly_value c_id;
	while (!null_p(candidate_ids)) {
		c_id = car(candidate_ids);
		sly_assert(is_subset(ss, syntax_scopes(max_id), syntax_scopes(c_id)),
				   "Error ambiguous binding");
		candidate_ids = cdr(candidate_ids);
	}
}


static sly_value
resolve(Sly_State *ss, sly_value id)
{
	sly_value candidate_ids = find_all_matching_bindings(ss, id);
	if (null_p(candidate_ids)) {
		sly_display(id, 1);
		printf("\n");
	}
	sly_assert(!null_p(candidate_ids), "Error unresolved identifier");
	sly_value max_id = get_max_id(candidate_ids);
	check_unambiguous(ss, max_id, candidate_ids);
	return dictionary_ref(all_bindings, max_id);
}

static sly_value
introduce(Sly_State *ss, sly_value s)
{
	return add_scope(ss, s, core_scope);
}

static sly_value
empty_env(Sly_State *ss)
{
	return make_dictionary(ss);
}

static sly_value
env_extend(Sly_State *ss, sly_value env, sly_value key, sly_value value)
{
	dictionary_set(ss, env, key, value);
	return env;
}

static sly_value
env_lookup(sly_value env, sly_value key)
{
	sly_value entry = dictionary_entry_ref(env, key);
	if (slot_is_free(entry)) {
		return SLY_VOID;
	}
	return cdr(entry);
}

static sly_value
csyntax(Sly_State *ss, sly_value datum, sly_value scope_set)
{
	sly_value stx = make_syntax(ss, (token){0}, datum);
	syntax *s = GET_PTR(stx);
	s->scope_set = scope_set;
	return stx;
}

static sly_value
expand_identifier(Sly_State *ss, sly_value s, sly_value env)
{
	sly_value binding = resolve(ss, s);
	sly_assert(!vector_contains(ss, core_forms, binding),
			   "Error identifier cannot be a core form");
	sly_value v = env_lookup(env, binding);
	if (sly_equal(v, variable)) return s;
	sly_assert(!void_p(v), "Error out of context");
	sly_assert(prototype_p(v)
			   || closure_p(v)
			   || cclosure_p(v)
			   || continuation_p(v),
			   "Error out of context");
	return s;
}

static sly_value
add_arg_scope(Sly_State *ss, sly_value args, sly_value sc)
{
	if (null_p(args)) return args;
	if (pair_p(args)) {
		return cons(ss,
					add_scope(ss, car(args), sc),
					add_arg_scope(ss, cdr(args), sc));
	}
	return add_scope(ss, args, sc);
}

static void
add_arg_bindings(Sly_State *ss, sly_value args, sly_value env)
{
	if (null_p(args)) return;
	if (pair_p(args)) {
		add_arg_bindings(ss, car(args), env);
		add_arg_bindings(ss, cdr(args), env);
	} else {
		sly_value binding = gensym(ss);
		add_binding(args, binding);
		env_extend(ss, env, binding, variable);
	}
}

static sly_value
expand_lambda(Sly_State *ss, sly_value s, sly_value env)
{
	sly_value lambda_id = car(s);
	sly_value arg_ids = car(cdr(s));
	sly_value body = cdr(cdr(s));
	sly_value sc = scope();
	sly_value ids = add_arg_scope(ss, arg_ids, sc);
	add_arg_bindings(ss, ids, env);
	return cons(ss, lambda_id,
				cons(ss, ids,
					 expand(ss, add_scope(ss, body, sc), env)));
}

static sly_value
expand_define(Sly_State *ss, sly_value s, sly_value env)
{
	sly_value define_id = car(s);
	sly_value lhs = car(cdr(s));
	sly_value rest = cdr(cdr(s));
	sly_value rhs = SLY_NULL;
	sly_value binding = gensym(ss);
	sly_value scopes = syntax_scopes(define_id);
	if (pair_p(lhs)) { // define procedure
		add_binding(car(lhs), binding);
		env_extend(ss, env, binding, variable);
		rhs = expand(ss, cons(ss, csyntax(ss, core_symbol(cf_lambda), scopes),
							  cons(ss, cdr(lhs), rest)), env);
		lhs = car(lhs);
	} else if (identifier_p(lhs)) {
		add_binding(lhs, binding);
		env_extend(ss, env, binding, variable);
		rhs = expand(ss, car(rest), env);
	} else {
		sly_assert(0, "Error bad syntax");
	}
	return cons(ss, define_id,
				cons(ss, lhs,
					 cons(ss, rhs, SLY_NULL)));
}

static sly_value
expand_define_syntax(Sly_State *ss, sly_value s, sly_value env)
{
	sly_value define_id = car(s);
	sly_value lhs = car(cdr(s));
	sly_value rest = cdr(cdr(s));
	sly_value rhs = SLY_NULL;
	sly_value binding = gensym(ss);
	sly_value scopes = syntax_scopes(define_id);
	if (pair_p(lhs)) { // define procedure
		add_binding(car(lhs), binding);
		env_extend(ss, env, binding, variable);
		rhs = expand(ss, cons(ss, csyntax(ss, core_symbol(cf_lambda), scopes),
							  cons(ss, cdr(lhs), rest)), env);
		lhs = car(lhs);
	} else if (identifier_p(lhs)) {
		add_binding(lhs, binding);
		env_extend(ss, env, binding, variable);
		rhs = expand(ss, car(rest), env);
	} else {
		sly_assert(0, "Error bad syntax");
	}
	env_extend(ss, env, binding,
			   sly_compile_lambda(ss, datum_to_syntax(ss, define_id, compile(ss, rhs))));
	return cons(ss, define_id,
				cons(ss, lhs,
					 cons(ss, rhs, SLY_NULL)));
}

static sly_value
expand_core_form(Sly_State *ss, sly_value s, sly_value env)
{
	sly_value id = car(s);
	sly_value exp_body = expand(ss, cdr(s), env);
	return cons(ss, id, exp_body);
}

static sly_value
expand_id_application_form(Sly_State *ss, sly_value s, sly_value env)
{
	sly_value binding = resolve(ss, car(s));
	if (sly_equal(binding, core_symbol(cf_lambda))) {
		return expand_lambda(ss, s, env);
	}
	if (sly_equal(binding, core_symbol(cf_define))) {
		return expand_define(ss, s, env);
	}
	if (sly_equal(binding, core_symbol(cf_define_syntax))) {
		return expand_define_syntax(ss, s, env);
	}
	if (sly_equal(binding, core_symbol(cf_quote))
		|| sly_equal(binding, core_symbol(cf_syntax_quote))) {
		return s;
	}
	if (vector_contains(ss, core_forms, binding)) {
		return expand_core_form(ss, s, env);
	}
	sly_value v = env_lookup(env, binding);
	if (prototype_p(v) || closure_p(v) || cclosure_p(v) || continuation_p(v)) {
		return expand(ss, apply_transformer(ss, v, s), env);
	}
	return expand_app(ss, s, env);
}

static sly_value
apply_transformer(Sly_State *ss, sly_value t, sly_value s)
{
	sly_value intro_scope = scope();
	sly_value intro_s = add_scope(ss, s, intro_scope);
	sly_value out = syntax_to_list(ss, macro_call(ss, t, intro_s));
	out = flip_scope(ss, out, intro_scope);
	// TODO: there is a bug with flip_scope
	return out;
}

static sly_value
expand_app(Sly_State *ss, sly_value s, sly_value env)
{
	if (null_p(s)) return s;
	sly_value head = expand(ss, car(s), env);
	sly_value rest = expand_app(ss, cdr(s), env);
	return cons(ss,	head, rest);
}

static sly_value
expand(Sly_State *ss, sly_value s, sly_value env)
{
	if (identifier_p(s)) {
		return expand_identifier(ss, s, env);
	} else if (pair_p(s) && identifier_p(car(s))) {
		return expand_id_application_form(ss, s, env);
	} else if (list_p(s)) {
		return expand_app(ss, s, env);
	}
#if 0
	sly_display(s, 1);
	printf("\n");
	sly_assert(0, "Error bad syntax");
#endif
	return s;
}

static sly_value
resolve_args(Sly_State *ss, sly_value args)
{
	if (null_p(args)) return args;
	if (pair_p(args)) {
		return cons(ss,
					resolve_args(ss, car(args)),
					resolve_args(ss, cdr(args)));
	} else {
		return resolve(ss, args);
	}
}


static sly_value
compile_lambda(Sly_State *ss, sly_value s)
{
	sly_value lambda_id = car(s);
	sly_value ids = car(cdr(s));
	sly_value body = cdr(cdr(s));
	return cons(ss, lambda_id,
				cons(ss, resolve_args(ss, ids),
					 compile(ss, body)));
}

static sly_value
compile(Sly_State *ss, sly_value s)
{
	sly_value r;
	if (identifier_p(s)) return resolve(ss, s);
	if (pair_p(s)) {
		if (identifier_p(car(s))) {
			r = resolve(ss, car(s));
			if (sly_equal(r, core_symbol(cf_lambda))) {
				return compile_lambda(ss, s);
			} else if (sly_equal(r, core_symbol(cf_quote))) {
				return s;
			} else if (sly_equal(r, core_symbol(cf_syntax_quote))) {
				return s;
			}
		}
		return cons(ss, compile(ss, car(s)), compile(ss, cdr(s)));
	}
	return s;
}

static void
hoist_top_level_defines(Sly_State *ss, sly_value s, sly_value env)
{
	sly_value binding, sym;
	while (!null_p(s)) {
		binding = car(s);
		if (identifier_p(binding)
			&& sly_equal(syntax_to_datum(binding), core_symbol(cf_begin))) {
			hoist_top_level_defines(ss, cdr(s), env);
		} else if (sly_equal(car(binding), core_symbol(cf_define))
				   || sly_equal(car(binding), core_symbol(cf_define_syntax))) {
			sym = car(cdr(s));
			if (pair_p(sym)) {
				sym = car(sym);
			}
			add_binding(csyntax(ss, sym, core_scope), SLY_NULL);
			env_extend(ss, env, sym, variable);
		}
		s = cdr(s);
	}
}

sly_value
sly_expand(Sly_State *ss, sly_value ast)
{
	all_bindings = make_dictionary(ss);
	core_forms = make_vector(ss, 0, CORE_FORM_COUNT);
	core_scope = scope();
	variable = gensym(ss);
	sly_value builtins = ss->cc->builtins;
	sly_value sym, scope_set;
	sly_value env = empty_env(ss);
	for (size_t i = 0; i < CORE_FORM_COUNT; ++i) {
		sym = make_symbol(ss, core_form_names[i], strlen(core_form_names[i]));
		vector_append(ss, core_forms, sym);
		scope_set = make_dictionary(ss);
		dictionary_set(ss, scope_set, core_scope, SLY_NULL);
		add_binding(csyntax(ss, sym, scope_set),
					sym);
	}
	while (!null_p(builtins)) {
		sym = car(car(builtins));
		scope_set = make_dictionary(ss);
		dictionary_set(ss, scope_set, core_scope, SLY_NULL);
		add_binding(csyntax(ss, sym, scope_set), sym);
		env_extend(ss, env, sym, variable);
		builtins = cdr(builtins);
	}
	sly_value before = ast;
	ast = introduce(ss, syntax_to_list(ss, ast));
	hoist_top_level_defines(ss, ast, env);
	ast = compile(ss, expand(ss, ast, env));
	printf("ast:\n");
	sly_display(ast, 1);
	printf("\n");
	ast = datum_to_syntax(ss, before, ast);
	return ast;
}
