#ifndef SCM_RUNTIME_H_
#define SCM_RUNTIME_H_

#define scm_assert(p, msg) _scm_assert(p, msg, __func__)

scm_value scm_runtime_load_dynamic(void);
scm_value scm_module_lookup(char *name, scm_value module);
int trampoline(scm_value cc);
scm_value _tail_call(scm_value proc);
void scm_heap_init(void);
void scm_chk_heap(scm_value *cc);
size_t mem_align_offset(size_t addr);
size_t scm_heap_alloc(size_t sz);
void _scm_assert(int p, char *msg, const char *func_name);
scm_value cons_rest(void);
scm_value _cons(scm_value car, scm_value cdr);
scm_value init_constant(struct constant cnst);
scm_value make_constant(size_t idx);
scm_value load_interned_constant(size_t idx);
scm_value *scm_intern_constants(struct constant *c, size_t len);
scm_value init_string(const STATIC_String *stc_str);
scm_value init_symbol(const STATIC_Symbol *stc_sym);
scm_value init_bytevector(const STATIC_Bytevector *stc_vu8);
scm_value init_vector(const STATIC_Vector *stc_vec);
scm_value init_list(const STATIC_List *stc_lst);
scm_value make_function(klabel_t fn);
scm_value make_closure(void);
klabel_t procedure_fn(scm_value value);
klabel_t function_fn(scm_value value);
klabel_t closure_fn(scm_value value);
scm_value closure_ref(scm_value clos, i32 idx);
void push_arg(scm_value x);
void push_ref(scm_value x);
scm_value pop_arg(void);
scm_value make_box(void);
void box_set(scm_value b, scm_value value);
scm_value box_ref(scm_value b);
scm_value make_int(i64 x);
scm_value make_char(i64 x);
scm_value make_float(f64 x);
f64 get_float(scm_value x);
int chk_args(size_t req, int rest);
scm_value primop_cons(void);
scm_value prim_cons(scm_value self);
scm_value primop_car(void);
scm_value prim_car(scm_value self);
scm_value primop_cdr(void);
scm_value prim_cdr(scm_value self);
scm_value primop_set_car(void);
scm_value prim_set_car(scm_value self);
scm_value primop_set_cdr(void);
scm_value prim_set_cdr(scm_value self);
scm_value primop_vector(void);
scm_value prim_vector(scm_value self);
/* void */
scm_value prim_void(scm_value self);
/* type predicates */
scm_value prim_boolean_p(scm_value self);
scm_value prim_char_p(scm_value self);
scm_value prim_null_p(scm_value self);
scm_value prim_pair_p(scm_value self);
scm_value prim_procedure_p(scm_value self);
scm_value prim_symbol_p(scm_value self);
scm_value prim_bytevector_p(scm_value self);
scm_value prim_number_p(scm_value self);
scm_value prim_string_p(scm_value self);
scm_value prim_vector_p(scm_value self);
scm_value prim_record_p(scm_value self);
#if 0
// unimplemented
scm_value prim_eof_object_p(scm_value self);
scm_value prim_port_p(scm_value self);
#endif
/* Equivalence predicates */
scm_value primop_eq(void);
scm_value prim_eq(scm_value self);
scm_value primop_eqv(void);
scm_value prim_eqv(scm_value self);
scm_value primop_equal(void);
scm_value prim_equal(scm_value self);
/* arithmetic procedures */
scm_value prim_inc(scm_value self);    // (1+ x)
scm_value prim_dec(scm_value self);    // (1- x)
scm_value primop_add(void);
scm_value prim_add(scm_value self);    // (+ x ...)
scm_value primop_sub(void);
scm_value prim_sub(scm_value self);    // (- x y ...)
scm_value primop_mul(void);
scm_value prim_mul(scm_value self);    // (* x ...)
scm_value primop_div(void);
scm_value prim_div(scm_value self);    // (/ x y ...)
scm_value primop_num_eq(void);
scm_value prim_num_eq(scm_value self); // (= x ...)
scm_value primop_less(void);
scm_value prim_less(scm_value self);   // (< x ...)
scm_value primop_gr(void);
scm_value prim_gr(scm_value self);     // (<= x ...)
scm_value primop_leq(void);
scm_value prim_leq(scm_value self);   // (>= x ...)
scm_value primop_geq(void);
scm_value prim_geq(scm_value self);   // (>= x ...)
scm_value primop_zero_p(void);
scm_value prim_zero_p(scm_value self); // (zero? x)
scm_value primop_positive_p(void);
scm_value prim_positive_p(scm_value self); // (positive? x)
scm_value primop_negative_p(void);
scm_value prim_negative_p(scm_value self); // (negative? x)
//////////////////////////////////////////////////////////
scm_value primop_string_len(void);
scm_value prim_string_len(scm_value self);
scm_value primop_string_ref(void);
scm_value prim_string_ref(scm_value self);
scm_value primop_string_eq(void);
scm_value prim_string_eq(scm_value self);
/* IO procedures */
scm_value primop_write(void);
scm_value prim_write(scm_value self);
scm_value primop_display(void);
scm_value prim_display(scm_value self);
scm_value primop_newline(void);
scm_value prim_newline(scm_value self);
void print_stk(void);

#endif /* SCM_RUNTIME_H_ */
