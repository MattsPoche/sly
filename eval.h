#ifndef SLY_EVAL_H_
#define SLY_EVAL_H_

sly_value call_closure(Sly_State *ss, sly_value call_list);
stack_frame *make_eval_stack(Sly_State *ss, sly_value regs);
sly_value eval_closure(Sly_State *ss, sly_value _clos, sly_value args, int gc_flag);

#endif /* SLY_EVAL_H_ */
