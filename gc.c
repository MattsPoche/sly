#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common_def.h"
#include "gc.h"
#include "sly_types.h"
#include "sly_alloc.h"
#include "opcodes.h"

void
gc_init(GC *gc)
{
	memset(gc, 0, sizeof(*gc));
}

void *
gc_alloc(struct _sly_state *ss, size_t size)
{
	GC *gc = &ss->gc;
	gc_object *obj = NULL;
	obj = MALLOC(size);
	obj->color = GC_WHITE;
	obj->next = gc->objects;
	obj->ngray = NULL;
	gc->objects = obj;
	gc->obj_count++;
	gc->tb += size;
	return obj;
}

static inline void
mark_gray(Sly_State *ss, gc_object *obj)
{
	if (obj == NULL) return;
	if (obj->color != GC_WHITE) return;
	obj->color = GC_GRAY;
	obj->ngray = ss->gc.grays;
	ss->gc.grays = obj;
}

static inline void
mark_object(gc_object *obj, u8 color)
{
	obj->color = color;
}

static void
traverse_frame(Sly_State *ss, stack_frame *frame)
{
	mark_object(GET_PTR(frame->code), GC_BLACK);
	mark_gray(ss, GET_PTR(frame->K));
	mark_gray(ss, GET_PTR(frame->U));
	mark_gray(ss, GET_PTR(frame->R));
	mark_gray(ss, GET_PTR(frame->clos));
	if (frame->parent) mark_gray(ss, (gc_object *)frame->parent);
}

static void
traverse_scope(Sly_State *ss, struct scope *scope)
{
	if (scope->parent) mark_gray(ss, (gc_object *)scope->parent);
	mark_gray(ss, GET_PTR(scope->proto));
	mark_gray(ss, GET_PTR(scope->symtable));
}

static void
traverse_pair(Sly_State *ss, pair *p)
{
	if (pair_p(p->car) || ptr_p(p->car)) {
		mark_gray(ss, GET_PTR(p->car));
	}
	if (pair_p(p->cdr) || ptr_p(p->cdr)) {
		mark_gray(ss, GET_PTR(p->cdr));
	}
}

static void
traverse_vector(Sly_State *ss, vector *vec)
{
	for (size_t i = 0; i < vec->len; ++i) {
		sly_value v = vec->elems[i];
		if (pair_p(v) || ptr_p(v)) {
			mark_gray(ss, GET_PTR(v));
		} else if (ref_p(v)) {
			sly_value *ref = GET_PTR(v);
			if (pair_p(*ref) || ptr_p(*ref)) {
				mark_gray(ss, GET_PTR(*ref));
			}
		}
	}
}

static void
traverse_dictionary(Sly_State *ss, vector *vec)
{
	for (size_t i = 0; i < vec->cap; ++i) {
		sly_value v = vec->elems[i];
		if (pair_p(v) || ptr_p(v)) {
			mark_gray(ss, GET_PTR(v));
		}
	}
}

static void
traverse_prototype(Sly_State *ss, prototype *proto)
{
	mark_object(GET_PTR(proto->uplist), GC_BLACK);
	mark_object(GET_PTR(proto->code), GC_BLACK);
	mark_gray(ss, GET_PTR(proto->K));
}

static void
traverse_closure(Sly_State *ss, closure *clos)
{
	mark_gray(ss, GET_PTR(clos->upvals));
	mark_gray(ss, GET_PTR(clos->proto));
}

static void
traverse_syntax(Sly_State *ss, syntax *stx)
{
	sly_value v = stx->datum;
	if (stx->scope) mark_gray(ss, (gc_object *)stx->scope);
	if (pair_p(v) || ptr_p(v)) {
		mark_gray(ss, GET_PTR(v));
	}
}

static void
traverse_syntax_transformer(Sly_State *ss, syntax_transformer *st)
{
	mark_gray(ss, GET_PTR(st->literals));
	mark_gray(ss, GET_PTR(st->clauses));
}

static void
traverse_continuation(Sly_State *ss, continuation *cont)
{
	if (cont->frame) {
		mark_gray(ss, (gc_object *)cont->frame);
	}
}

static void
traverse_object(Sly_State *ss, gc_object *obj)
{
	mark_object(obj, GC_BLACK);
	switch ((enum type_tag)obj->type) {
	case tt_pair:
		traverse_pair(ss, (pair *)obj);
		break;
	case tt_byte: break;
	case tt_int: break;
	case tt_float: break;
	case tt_symbol: break;
	case tt_string: break;
	case tt_byte_vector: break;
	case tt_vector:
		traverse_vector(ss, (vector *)obj);
		break;
	case tt_dictionary:
		traverse_dictionary(ss, (vector *)obj);
		break;
	case tt_prototype:
		traverse_prototype(ss, (prototype *)obj);
		break;
	case tt_closure:
		traverse_closure(ss, (closure *)obj);
		break;
	case tt_cclosure: break;
	case tt_continuation:
		traverse_continuation(ss, (continuation *)obj);
		break;
	case tt_syntax:
		traverse_syntax(ss, (syntax *)obj);
		break;
	case tt_syntax_transformer:
		traverse_syntax_transformer(ss, (syntax_transformer *)obj);
		break;
	case tt_scope:
		traverse_scope(ss, (struct scope *)obj);
		break;
	case tt_stack_frame:
		traverse_frame(ss, (stack_frame *)obj);
		break;
	}
}

static void
propagate_mark(Sly_State *ss)
{
	GC *gc = &ss->gc;
	if (gc->grays) {
		gc_object *obj = gc->grays;
		gc->grays = obj->ngray;
		obj->ngray = NULL;
		traverse_object(ss, obj);
	}
}

static void
mark_roots(Sly_State *ss)
{
	traverse_object(ss, (gc_object *)ss->frame);
	traverse_object(ss, (gc_object *)ss->proto);
	traverse_object(ss, (gc_object *)ss->interned);
	traverse_object(ss, (gc_object *)ss->cc->globals);
}

static void
free_object(Sly_State *ss, gc_object *obj)
{
	switch (obj->type) {
	case tt_symbol: {
		symbol *sym = (symbol *)obj;
		FREE(sym->name);
	} break;
	case tt_string:
	case tt_byte_vector: {
		byte_vector *bvec = (byte_vector *)obj;
		FREE(bvec->elems);
	} break;
	case tt_dictionary:
	case tt_vector: {
		vector *vec = (vector *)obj;
		FREE(vec->elems);
	} break;
	}
	FREE(obj);
	ss->gc.obj_freed++;
}

static void
sweep(Sly_State *ss)
{
	GC *gc = &ss->gc;
	gc_object *obj = gc->objects;
	gc_object *tmp = NULL, *prev = NULL;
	while (obj) {
		if (obj->color == GC_WHITE) {
			tmp = obj;
			obj = obj->next;
			if (prev == NULL) {
				gc->objects = obj;
			} else {
				prev->next = obj;
			}
			free_object(ss, tmp);
		} else {
			mark_object(obj, GC_WHITE);
			prev = obj;
			obj = obj->next;
		}
	}
}

void
gc_collect(Sly_State *ss)
{
	GC *gc = &ss->gc;
	gc->tb = 0;
	gc->collections++;
	mark_roots(ss);
	while (gc->grays) {
		propagate_mark(ss);
	}
	sweep(ss);
}

void
gc_free_all(Sly_State *ss)
{
	gc_object *tmp, *obj = ss->gc.objects;
	while (obj) {
		tmp = obj;
		obj = obj->next;
		free_object(ss, tmp);
	}
	ss->gc.objects = NULL;
}
