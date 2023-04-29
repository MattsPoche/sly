#ifndef SLY_COMPILE_H_
#define SLY_COMPILE_H_

union uplookup {
	sly_value v;
	struct {
		u8 isup;    /* points to upval */
		u8 reg;     /* register */
	} u;
};

struct compile {
	sly_value globals;  // <dictionary>
	struct scope *cscope;
};

void sly_init_state(Sly_State *ss);
void sly_free_state(Sly_State *ss);
void sly_do_file(char *file_path, int debug_info);
int comp_lambda(Sly_State *ss, sly_value form, int reg);
int comp_expr(Sly_State *ss, sly_value form, int reg);
int sly_compile(Sly_State *ss, sly_value ast);
sly_value strip_syntax(Sly_State *ss, sly_value form);

#endif /* SLY_COMPILE_H_ */
