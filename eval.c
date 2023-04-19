#include "sly_types.h"
#include "sly_vm.h"
#include "parser.h"
#include "sly_compile.h"
#define OPCODES_INCLUDE_INLINE 1
#include "opcodes.h"
#include "eval.h"

static sly_value
call_closure(Sly_State *ss, sly_value clos, sly_value arglist)
{
	stack_frame *frame = ss->frame;
	size_t pc = frame->pc;
	sly_value code = frame->code;
	sly_value R = frame->R;
	frame->R = make_vector(ss, 0, 12);
	frame->code = make_vector(ss, 0, 2);
	frame->pc = 0;
	vector_append(ss, frame->R, clos);
	while (!null_p(arglist)) {
		vector_append(ss, frame->R, eval_expr(ss, car(arglist)));
		arglist = cdr(arglist);
	}
	size_t len = vector_len(frame->R);
	vector_append(ss, frame->code, iAB(OP_CALL, 0, len, -1));
	vector_append(ss, frame->code, iA(OP_RETURN, 0, -1));
	sly_value val = vm_run(ss, 0);
	frame->R = R;
	frame->code = code;
	frame->pc = pc;
	ss->frame = frame;
	return val;
}

static sly_value
eval_lambda(Sly_State *ss, sly_value expr)
{
	comp_lambda(ss, expr, 0);
	size_t len = vector_len(ss->frame->K);
	sly_value proto = vector_ref(ss->frame->K, len-1);
	return form_closure(ss, proto);
}

static sly_value
eval_definition(Sly_State *ss, sly_value expr)
{
	sly_value head = car(expr);
	sly_value tail = cdr(expr);
	sly_value globals = ss->cc->globals;
	sly_value name, val;
	if (pair_p(head)) {	/* procedure */
		name = syntax_to_datum(car(head));
		sly_assert(symbol_p(name), "Error (eval_definition): variable name must be a symbol");
		val = eval_lambda(ss, cons(ss, cdr(head), tail));
	} else { /* variable */
		name = syntax_to_datum(head);
		sly_assert(symbol_p(name), "Error (eval_definition): variable name must be a symbol");
		val = eval_expr(ss, car(tail));
		sly_assert(null_p(cdr(tail)), "Error (eval_definition): bad syntax, expected end of expr");
	}
	dictionary_set(ss, globals, name, val);
	return SLY_VOID;
}

static sly_value
eval_list(Sly_State *ss, sly_value expr)
{
	sly_value head = car(expr);
	sly_value tail = cdr(expr);
	sly_value fn;
	if (pair_p(head)) {
		fn = eval_expr(ss, head);
	} else {
		sly_value datum = syntax_to_datum(head);
		/* check for keyword */
		if (symbol_eq(datum, cstr_to_symbol("define"))
			|| symbol_eq(datum, cstr_to_symbol("set!"))) {
			return eval_definition(ss, tail);
		} else if (symbol_eq(datum, cstr_to_symbol("quote"))) {
			sly_value val = strip_syntax(ss, car(tail));
			sly_assert(null_p(cdr(tail)), "Error (eval_list): bad syntax, expected end of expr");
			return val;
		} else if (symbol_eq(datum, cstr_to_symbol("syntax-quote"))) {
			sly_assert(null_p(cdr(tail)), "Error (eval_list): bad syntax, expected end of expr");
			return car(tail);
		} else if (symbol_eq(datum, cstr_to_symbol("begin"))) {
			sly_value val;
			while (!null_p(tail)) {
				val = eval_expr(ss, car(tail));
				tail = cdr(tail);
			}
			return val;
		} else if (symbol_eq(datum, cstr_to_symbol("if"))) {
			sly_value clause = car(tail);
			tail = cdr(tail);
			sly_value tbranch = car(tail);
			tail = cdr(tail);
			sly_value fbranch = car(tail);
			sly_assert(null_p(cdr(tail)), "Error (eval_list): bad syntax, expected end of expr");
			if (booltoc(eval_expr(ss, clause))) {
				return eval_expr(ss, tbranch);
			} else {
				return eval_expr(ss, fbranch);
			}
		} else if (symbol_eq(datum, cstr_to_symbol("lambda"))) {
			return eval_lambda(ss, tail);
		}
		/* otherwise eval atom */
		fn = eval_expr(ss, head);
	}
	/* if we reach this point, assume fn is a closure */
	return call_closure(ss, fn, tail);
}

static sly_value
eval_atom(Sly_State *ss, sly_value expr)
{
	sly_value datum = syntax_to_datum(expr);
	sly_value globals = ss->cc->globals;
	if (symbol_p(datum)) {
		return dictionary_ref(globals, datum);
	} else {
		return datum;
	}
}

sly_value
eval_expr(Sly_State *ss, sly_value expr)
{
	if (pair_p(expr)) {
		return eval_list(ss, expr);
	} else {
		return eval_atom(ss, expr);
	}
}
