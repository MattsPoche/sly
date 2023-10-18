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
	sly_value builtins;  // <dictionary>
	struct scope *cscope;
};

void sly_init_state(Sly_State *ss);
void sly_free_state(Sly_State *ss);
sly_value sly_expand_only(Sly_State *ss, char *file_path);
void sly_do_file(char *file_path, int debug_info);
int comp_expr(Sly_State *ss, sly_value form, int reg);
sly_value sly_compile_lambda(Sly_State *ss, sly_value ast);
sly_value sly_compile(Sly_State *ss, sly_value ast);
sly_value strip_syntax(sly_value form);

#endif /* SLY_COMPILE_H_ */
