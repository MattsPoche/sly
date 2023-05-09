#ifndef SLY_VM_H_
#define SLY_VM_H_

sly_value form_closure(Sly_State *ss, sly_value _proto);
sly_value vm_run(Sly_State *ss, int run_gc);

#endif /* SLY_VM_H_ */
