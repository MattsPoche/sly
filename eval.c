#include "sly_types.h"
#include "sly_vm.h"
#include "parser.h"
#include "sly_compile.h"
#define OPCODES_INCLUDE_INLINE 1
#include "opcodes.h"
#include "eval.h"

#define CAR(val) (syntax_p(val) ? car(syntax_to_datum(val)) : car(val))
#define CDR(val) (syntax_p(val) ? cdr(syntax_to_datum(val)) : cdr(val))
#define syntax_cons(car, cdr) make_syntax(ss, (token){0}, cons(ss, car, cdr))

sly_value
call_closure(Sly_State *ss, sly_value clos, sly_value arglist)
{
	stack_frame *frame = NULL;
	if (ss->frame) {
		frame = ss->frame;
		ss->frame = ss->eval_frame;
		ss->frame->K = frame->K;
		ss->frame->U = frame->U;
	} else {
		ss->frame = ss->eval_frame;
		ss->frame->K = make_vector(ss, 0, 12);
		ss->frame->U = make_vector(ss, 1, 1);
		vector_set(ss->frame->U, 0, ss->cc->globals);
	}
	vector_discard_values(ss, ss->frame->code);
	vector_discard_values(ss, ss->frame->R);
	ss->frame->pc = 0;
	vector_append(ss, ss->frame->R, clos);
	while (!null_p(arglist)) {
		vector_append(ss, ss->frame->R, eval_expr(ss, CAR(arglist)));
		arglist = cdr(arglist);
	}
	size_t len = vector_len(ss->frame->R);
	vector_append(ss, ss->frame->code, iAB(OP_CALL, 0, len, -1));
	vector_append(ss, ss->frame->code, iA(OP_RETURN, 0, -1));
	sly_value val = vm_run(ss, 0);
	ss->frame = frame;
	return val;
}

sly_value
call_closure_no_eval(Sly_State *ss, sly_value clos, sly_value arglist)
{
	stack_frame *frame = NULL;
	if (ss->frame) {
		frame = ss->frame;
		ss->frame = ss->eval_frame;
		ss->frame->K = frame->K;
		ss->frame->U = frame->U;
	} else {
		ss->frame = ss->eval_frame;
		ss->frame->K = make_vector(ss, 0, 12);
		ss->frame->U = make_vector(ss, 1, 1);
		vector_set(ss->frame->U, 0, ss->cc->globals);
	}
	vector_discard_values(ss, ss->frame->code);
	vector_discard_values(ss, ss->frame->R);
	ss->frame->pc = 0;
	vector_append(ss, ss->frame->R, clos);
  	while (!null_p(arglist)) {
		vector_append(ss, ss->frame->R, car(arglist));
		arglist = cdr(arglist);
	}
	size_t len = vector_len(ss->frame->R);
	vector_append(ss, ss->frame->code, iAB(OP_CALL, 0, len, -1));
	vector_append(ss, ss->frame->code, iA(OP_RETURN, 0, -1));
	sly_value val = vm_run(ss, 0);
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
	sly_value head = CAR(expr);
	sly_value tail = CDR(expr);
	sly_value globals = ss->cc->globals;
	sly_value name, val;
	if (syntax_pair_p(head)) {	/* procedure */
		name = syntax_to_datum(CAR(head));
		sly_assert(symbol_p(name), "Error (eval_definition): variable name must be a symbol");
		val = eval_lambda(ss, cons(ss, CDR(head), tail));
	} else { /* variable */
		name = syntax_to_datum(head);
		sly_assert(symbol_p(name), "Error (eval_definition): variable name must be a symbol");
		val = eval_expr(ss, CAR(tail));
		sly_assert(null_p(CDR(tail)), "Error (eval_definition): bad syntax, expected end of expr");
	}
	dictionary_set(ss, globals, name, val);
	return SLY_VOID;
}

static sly_value
eval_list(Sly_State *ss, sly_value expr)
{
	sly_value head = CAR(expr);
	sly_value tail = CDR(expr);
	sly_value fn;
	if (syntax_pair_p(head)) {
		fn = eval_expr(ss, head);
	} else {
		sly_value datum = syntax_to_datum(head);
		/* check for keyword */
		if (symbol_eq(datum, cstr_to_symbol("define"))
			|| symbol_eq(datum, cstr_to_symbol("set!"))) {
			return eval_definition(ss, tail);
		} else if (symbol_eq(datum, cstr_to_symbol("quote"))) {
			sly_value val = strip_syntax(ss, CAR(tail));
			sly_assert(null_p(CDR(tail)), "Error (eval_list): bad syntax, expected end of expr");
			return val;
		} else if (symbol_eq(datum, cstr_to_symbol("syntax-quote"))) {
			sly_assert(null_p(CDR(tail)), "Error (eval_list): bad syntax, expected end of expr");
			return CAR(tail);
		} else if (symbol_eq(datum, cstr_to_symbol("begin"))) {
			sly_value val = SLY_VOID;
			while (!null_p(tail)) {
				val = eval_expr(ss, CAR(tail));
				tail = CDR(tail);
			}
			return val;
		} else if (symbol_eq(datum, cstr_to_symbol("if"))) {
			sly_value clause = CAR(tail);
			tail = CDR(tail);
			sly_value tbranch = CAR(tail);
			tail = CDR(tail);
			sly_value fbranch = CAR(tail);
			sly_assert(null_p(CDR(tail)), "Error (eval_list): bad syntax, expected end of expr");
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
	} else if (null_p(datum)) {
		sly_assert(0, "Error (eval_atom): bad syntax, empty expression");
	}
	return datum;
}

sly_value
eval_expr(Sly_State *ss, sly_value expr)
{
	if (syntax_pair_p(expr)) {
		return eval_list(ss, expr);
	} else {
		return eval_atom(ss, expr);
	}
}
