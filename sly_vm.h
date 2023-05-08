#ifndef SLY_VM_H_
#define SLY_VM_H_

sly_value form_closure(Sly_State *ss, sly_value _proto);
sly_value vm_run(Sly_State *ss, int run_gc);
sly_value vm_run_procedure(Sly_State *ss, u32 idx, u32 nargs);

#endif /* SLY_VM_H_ */
