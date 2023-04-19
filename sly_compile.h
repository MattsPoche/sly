#ifndef SLY_COMPILE_H_
#define SLY_COMPILE_H_

struct uplookup {
	u32 level;  /* scope level */
	u8 isup;    /* points to upval (or function arg) */
	u8 reg;     /* register */
	u8 _pad[2];
};

struct compile {
	sly_value globals;  // <dictionary>
	struct scope *cscope;
};

int comp_lambda(Sly_State *ss, sly_value form, int reg);
int comp_expr(Sly_State *ss, sly_value form, int reg);
int sly_compile(Sly_State *ss, sly_value ast);
sly_value strip_syntax(Sly_State *ss, sly_value form);

#endif /* SLY_COMPILE_H_ */
