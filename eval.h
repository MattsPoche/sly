#ifndef SLY_EVAL_H_
#define SLY_EVAL_H_

sly_value call_closure(Sly_State *ss, sly_value clos, sly_value arglist);
sly_value call_closure_no_eval(Sly_State *ss, sly_value clos, sly_value arglist);
sly_value eval_expr(Sly_State *ss, sly_value expr);

#endif /* SLY_EVAL_H_ */
