#ifndef SLY_COMPILE_H_
#define SLY_COMPILE_H_

struct uplookup {
	u32 level;  /* scope level */
	u8 isup;    /* points to upval (or function arg) */
	u8 reg;     /* register */
	u8 _pad[2];
};

struct scope {
	struct scope *parent; // NULL if top-level
	sly_value proto;      // <prototype>
	sly_value symtable;   // <dictionary>
	u32 level;
	int prev_var;
};

struct compile {
	sly_value interned; // <dictionary> (<string> . <symbol>)
	sly_value globals;  // <dictionary>
	struct scope *cscope;
};

void sly_compile(Sly_State *ss, char *file_name);

#endif /* SLY_COMPILE_H_ */
