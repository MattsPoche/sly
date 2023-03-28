#ifndef SLY_COMPILE_H_
#define SLY_COMPILE_H_

struct scope {
	struct scope *parent; // NULL if top-level
	sly_value proto;      // <prototype>
	sly_value symtable;   // <dictionary>
};

struct compile {
	sly_value interned; // <dictionary> (<string> . <symbol>)
	struct scope *cscope;
};

struct compile sly_compile(char *file_name);

#endif /* SLY_COMPILE_H_ */
