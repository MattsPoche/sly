#include <stdio.h>
#include <gc.h>
#include "sly_types.h"
#include "parser.h"
#include "sly_compile.h"
#include "opcodes.h"
#include "eval.h"
#include "sly_vm.h"

int allocations = 0;
int net_allocations = 0;
size_t bytes_allocated = 0;

static char *
next_arg(int *argc, char **argv[])
{
	if (argc == 0) {
		return NULL;
	}
	char *arg = **argv;
	(*argc)--;
	(*argv)++;
	return arg;
}

#if 0
static sly_value M(Sly_State *ss, sly_value expr);
static sly_value T(Sly_State *ss, sly_value expr, sly_value cont);

static sly_value
M(Sly_State *ss, sly_value expr)
{
	if (pair_p(expr)) {
		sly_value fst = car(expr);
		sly_value rest = cdr(expr) ;
		if (symbol_p(fst)) {
			sly_value lambda = make_symbol(ss, "lambda", 6);
			if (symbol_eq(fst, make_symbol(ss, "quote", 5))) {
				return expr;
			} else if (symbol_eq(fst, lambda)) {
				sly_value args = car(rest);
				rest = cdr(rest);
				sly_value k = gensym_from_cstr(ss, "k");
				args = cons(ss, k, args);
				expr = T(ss, rest, k);
				return cons(ss, fst, cons(ss, args, cons(ss, expr, SLY_NULL)));
			} else if (symbol_eq(fst, make_symbol(ss, "begin", 5))) {
				sly_value k = gensym_from_cstr(ss, "k");
				expr = T(ss, rest, k);
				return cons(ss, lambda, cons(ss, SLY_NULL, cons(ss, expr, SLY_NULL)));
			} else {
				sly_displayln(expr);
				sly_assert(0, "Somethings wrong 1");
			}
		} else {
			sly_displayln(expr);
			sly_assert(0, "Somethings wrong 2");
		}
	}
	return expr;
}

static sly_value
T(Sly_State *ss, sly_value expr, sly_value cont)
{
	if (pair_p(expr)) {
		sly_value fst = car(expr);
		if (symbol_p(fst)) {
			sly_value lambda = make_symbol(ss, "lambda", 6);
			if (symbol_eq(fst, make_symbol(ss, "quote", 5))) {
				return cons(ss, cont, cons(ss, M(ss, expr), SLY_NULL));
			} else if (symbol_eq(fst, lambda)) {
				return cons(ss, cont, cons(ss, M(ss, expr), SLY_NULL));
			} else {
				sly_value f = car(expr);
				sly_value e = cdr(expr);
				sly_value id_f = gensym_from_cstr(ss, "f");
				sly_value id_e = gensym_from_cstr(ss, "e");
				return T(ss, f, cons(ss, lambda,
									 cons(ss,
										  cons(ss, id_f, SLY_NULL),
										  T(ss, e, cons(ss, lambda,
														cons(ss, cons(ss, id_e, SLY_NULL),
															 cons(ss, cons(ss, id_f,
																		   cons(ss, id_e,
																				cons(ss, cont, SLY_NULL))),
																  SLY_NULL)))))));
			}
		}
	}
	return cons(ss, cont, cons(ss, M(ss, expr), SLY_NULL));
}
#endif

int
main(int argc, char *argv[])
{
	char *arg = next_arg(&argc, &argv);
	int debug_info = 0;
	if (argc) {
		while (argc) {
			arg = next_arg(&argc, &argv);
			if (strcmp(arg, "-I") == 0) {
				debug_info = 1;
			} else if (strcmp(arg, "--expand") == 0) {
				Sly_State ss = {0};
				sly_value ast = sly_expand_only(&ss, next_arg(&argc, &argv));
				ast = strip_syntax(&ss, ast);
				sly_displayln(ast);
			} else {
				printf("Running file %s ...\n", arg);
				sly_do_file(arg, debug_info);
				printf("\n");
			}
		}
	} else {
		printf("No source file provided.\nExiting ...\n");
	}
	return 0;
}
