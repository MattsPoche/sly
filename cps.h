#ifndef SLY_CPS_H_
#define SLY_CPS_H_

enum cps_type {
	tt_cps_const,
	tt_cps_call,
	tt_cps_kcall,
	tt_cps_proc,
	tt_cps_prim,
	tt_cps_primcall,
	tt_cps_values,
	tt_cps_make_record,
	tt_cps_record,
	tt_cps_record_set,
	tt_cps_select,
	tt_cps_offset,
	tt_cps_code,    // code ptr
	tt_cps_box,
	tt_cps_unbox,
	tt_cps_set,
	tt_cps_fix,
	tt_cps_branch,
	tt_cps_continue,
	tt_cps_kargs,
	tt_cps_kreceive,
	tt_cps_kproc,
	tt_cps_ktail,
};

enum prim {
	tt_prim_void = 0,
	tt_prim_add,
	tt_prim_sub,
	tt_prim_mul,
	tt_prim_div,
	tt_prim_idiv,
	tt_prim_mod,
	tt_prim_bw_and,
	tt_prim_bw_ior,
	tt_prim_bw_xor,
	tt_prim_bw_eqv,
	tt_prim_bw_nor,
	tt_prim_bw_nand,
	tt_prim_bw_not,
	tt_prim_bw_shift,
	tt_prim_eq,           // eq?
	tt_prim_eqv,
	tt_prim_equal,
	tt_prim_num_eq,       // =
	tt_prim_less,         // <
	tt_prim_gr,           // >
	tt_prim_leq,          // <=
	tt_prim_geq,          // >=
	tt_prim_apply,
	tt_prim_cons,
	tt_prim_car,
	tt_prim_cdr,
	tt_prim_list,
	tt_prim_vector,
	tt_prim_vector_ref,
	tt_prim_vector_set,
};

typedef sly_value (*fn_primop)(Sly_State *ss, sly_value arg_list);

struct primop {
	char *cstr;
	sly_value name;
	fn_primop fn;
};

struct arity_t {
	sly_value req;
	sly_value rest;
};

/* continuations */
typedef struct _cps_kargs {
	sly_value vars;          // list of variables
	struct _cps_term *term;
} CPS_Kargs;

typedef struct _cps_kreceive {
	struct arity_t arity;
	sly_value k;            // symbol
} CPS_Kreceive;

typedef struct _cps_Kproc {
	struct arity_t arity;
	sly_value binding;
	sly_value tail;
	sly_value body;
} CPS_Kproc;

struct kclosure_t {
	sly_value clos_shares;
	sly_value clos_def;
	sly_value cc_name;
	sly_value kr_name;
	int offset;
	int kr_size;
};

typedef struct _cps_cont {
	int type;
	sly_value name;  // symbol
	union {
		CPS_Kargs kargs;
		CPS_Kreceive kreceive;
		CPS_Kproc kproc;
	} u;
} CPS_Kont;

/* terms */
typedef struct _cps_continue {
	sly_value k;
	struct _cps_expr *expr;
} CPS_Continue;

typedef struct _cps_branch {
	sly_value arg;
	sly_value kt;
	sly_value kf;
} CPS_Branch;

typedef struct _cps_term {
	int type;
	union {
		CPS_Continue cont;
		CPS_Branch branch;
	} u;
} CPS_Term;

/* expressions */
typedef struct _cps_values {
	sly_value args;
} CPS_Values;

typedef struct _cps_const {
	sly_value value;
} CPS_Const;

typedef struct _cps_proc {
	sly_value k;    // kfun
} CPS_Proc;

typedef struct _cps_prim {
	sly_value name;
} CPS_Prim;

typedef struct _cps_primcall {
	sly_value prim;
	sly_value args;
} CPS_Primcall;

typedef struct _cps_call {
	sly_value proc;
	sly_value args;
} CPS_Call;

typedef struct _cps_kcall {
	sly_value proc_name;
	sly_value label;
	sly_value args;
} CPS_Kcall;

typedef struct _cps_make_record { // allocate record of n fields
	int nfields;
} CPS_Make_Record;

typedef struct _cps_record { // used for closure creation
	sly_value values;
} CPS_Record;

typedef struct _cps_record_set { // initialize member of record after record creation
	sly_value record;
	sly_value val;
	int field;
} CPS_Record_Set;

typedef struct _cps_select {
	sly_value record;
	int field;
} CPS_Select;

typedef struct _cps_offset {
	sly_value record;
	int off;
} CPS_Offset;

typedef struct _cps_code {
	sly_value label;
} CPS_Code;

typedef struct _cps_box {
	sly_value val;
} CPS_Box;

typedef struct _cps_unbox {
	sly_value var;
} CPS_Unbox;

typedef struct _cps_set {
	sly_value var;
	sly_value val;
} CPS_Set;

typedef struct _cps_fix {
	sly_value names;  // list of names
	sly_value procs;  // list of proc exprs
} CPS_Fix;

typedef struct _cps_expr {
	int type;
	union {
		CPS_Call call;
		CPS_Kcall kcall;
		CPS_Const constant;
		CPS_Proc proc;
		CPS_Prim prim;
		CPS_Primcall primcall;
		CPS_Values values;
		CPS_Make_Record make_record;
		CPS_Record record;
		CPS_Record_Set record_set;
		CPS_Select select;
		CPS_Offset offset;
		CPS_Code code;
		CPS_Box box;
		CPS_Unbox unbox;
		CPS_Set set;
		CPS_Fix fix;
	} u;
} CPS_Expr;

typedef struct _var_info {
	int used;     /* Number of times a variable is referenced */
	int escapes;  /* number of times a variable is loaded into a datastructure or passed as a parameter */
	int updates;  /* number of times a variable is set! */
	int isarg;
	int isalias;
	int which;
	CPS_Expr *binding;
	struct _var_info *alt;
} CPS_Var_Info;

CPS_Kont *cps_graph_ref(sly_value graph, sly_value k);
void cps_graph_set(Sly_State *ss, sly_value graph, sly_value k, CPS_Kont *kont);
int cps_graph_is_member(sly_value graph, sly_value k);
CPS_Term *cps_new_term(void);
CPS_Expr *cps_make_constant(sly_value value);
CPS_Expr *cps_new_expr(void);
CPS_Expr *cps_make_fix(void);
CPS_Var_Info * cps_new_var_info(CPS_Expr *binding, int isarg, int isalias, int which);
CPS_Kont *cps_make_kargs(Sly_State *ss, sly_value name, CPS_Term *term, sly_value vars);
CPS_Kont *cps_make_ktail(Sly_State *ss, int genname);
sly_value cps_collect_var_info(Sly_State *ss, sly_value graph, sly_value global_tbl,
							   sly_value state, sly_value prev_tbl, CPS_Expr *expr, sly_value k);
sly_value cps_collect_free_variables(Sly_State *ss, sly_value graph,
									 sly_value var_info, sly_value k);
sly_value cps_opt_contraction_phase(Sly_State *ss, sly_value graph, sly_value k, int debug);
sly_value cps_opt_closure_convert(Sly_State *ss, sly_value graph, struct kclosure_t *clos,
								  sly_value free_var_lookup, sly_value free_vars, sly_value k);
void cps_init_primops(Sly_State *ss);
sly_value cps_translate(Sly_State *ss, sly_value cc, sly_value graph, sly_value form);
void cps_display(Sly_State *ss, sly_value graph, sly_value k);
void cps_display_var_info(Sly_State *ss, sly_value var_info);
sly_value cps_free_vars_in_k(Sly_State *ss, sly_value graph, sly_value k);
void cps_display_free_vars_foreach_k(Sly_State *ss, sly_value graph, sly_value k);
int primop_p(sly_value name);

#endif /* SLY_CPS_H_ */
