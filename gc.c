#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common_def.h"
#include "gc.h"
#include "sly_types.h"
#include "sly_alloc.h"
#include "opcodes.h"

#define GC_WHITE 0
#define GC_GRAY  1
#define GC_BLACK 2

#define mark_gray_safe(ss, v) if (heap_obj_p(v)) mark_gray(ss, GET_PTR(v))


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
	gc->bytes += size;
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
	if (frame->parent) {
		mark_gray(ss, (gc_object *)frame->parent);
	}
}

static void
traverse_scope(Sly_State *ss, struct scope *scope)
{
	mark_gray(ss, (gc_object *)scope->parent);
	mark_gray(ss, GET_PTR(scope->proto));
	mark_gray(ss, GET_PTR(scope->symtable));
	mark_gray(ss, GET_PTR(scope->macros));
}

static void
traverse_pair(Sly_State *ss, pair *p)
{
	mark_gray_safe(ss, p->car);
	mark_gray_safe(ss, p->cdr);
}

static void
traverse_vector(Sly_State *ss, vector *vec)
{
	for (size_t i = 0; i < vec->len; ++i) {
		mark_gray_safe(ss, vec->elems[i]);
	}
}

static void
traverse_dictionary(Sly_State *ss, vector *vec)
{
	for (size_t i = 0; i < vec->cap; ++i) {
		mark_gray_safe(ss, vec->elems[i]);
	}
}

static void
traverse_prototype(Sly_State *ss, prototype *proto)
{
	mark_object(GET_PTR(proto->uplist), GC_BLACK);
	mark_object(GET_PTR(proto->code), GC_BLACK);
	mark_gray(ss, GET_PTR(proto->K));
	mark_gray(ss, GET_PTR(proto->syntax_info));
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
	mark_gray_safe(ss, stx->datum);
	mark_gray(ss, GET_PTR(stx->lex_info));
}

static void
traverse_continuation(Sly_State *ss, continuation *cont)
{
	if (cont->frame) {
		mark_gray(ss, (gc_object *)cont->frame);
	}
}

static void
traverse_upvalue(Sly_State *ss, upvalue *uv)
{
	mark_gray(ss, (gc_object *)uv->next);
	if (uv->isclosed) {
		mark_gray_safe(ss, uv->u.val);
	} else {
		mark_gray_safe(ss, *uv->u.ptr);
	}
}

static void
traverse_object(Sly_State *ss, gc_object *obj)
{
	if (obj == NULL || obj->color == GC_BLACK) {
		return;
	}
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
	case tt_upvalue:
		traverse_upvalue(ss, (upvalue *)obj);
		break;
	case tt_syntax:
		traverse_syntax(ss, (syntax *)obj);
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
	traverse_object(ss, (gc_object *)ss->modules);
	traverse_object(ss, GET_PTR(ss->cc->globals));
	traverse_object(ss, GET_PTR(ss->cc->builtins));
	traverse_object(ss, (gc_object *)ss->cc->cscope);
	traverse_object(ss, (gc_object *)ss->open_upvals);
	/*
	if (ss->frame != NULL) mark_object((gc_object *)ss->frame, GC_GRAY);
	if (ss->eval_frame != NULL) mark_object((gc_object *)ss->eval_frame, GC_GRAY);
	if (ss->cc->cscope != NULL) mark_object((gc_object *)ss->cc->cscope, GC_GRAY);
	if (ss->open_upvals != NULL) mark_object((gc_object *)ss->open_upvals, GC_GRAY);
	mark_object(GET_PTR(ss->proto), GC_GRAY);
	mark_object(GET_PTR(ss->interned), GC_GRAY);
	mark_object(GET_PTR(ss->cc->globals), GC_GRAY);
	*/
}

static void
free_object(Sly_State *ss, gc_object *obj)
{
	size_t size = 0;
	switch ((enum type_tag)obj->type) {
	case tt_pair: {
		size = sizeof(pair);
	} break;
	case tt_byte:
	case tt_int:
	case tt_float: {
		size = sizeof(number);
	} break;
	case tt_prototype: {
		size = sizeof(prototype);
	} break;
	case tt_upvalue: {
		size = sizeof(upvalue);
	} break;
	case tt_closure: {
		size = sizeof(closure);
	} break;
	case tt_cclosure: {
		size = sizeof(cclosure);
	} break;
	case tt_continuation: {
		size = sizeof(continuation);
	} break;
	case tt_syntax: {
		size = sizeof(syntax);
	} break;
	case tt_scope: {
		size = sizeof(struct scope);
	} break;
	case tt_stack_frame: {
		size = sizeof(stack_frame);
	} break;
	case tt_symbol: {
		symbol *sym = (symbol *)obj;
		size = sizeof(*sym);
		ss->gc.bytes -= sym->len;
		FREE(sym->name);
	} break;
	case tt_string:
	case tt_byte_vector: {
		byte_vector *bvec = (byte_vector *)obj;
		size = sizeof(*bvec);
		ss->gc.bytes -= bvec->cap;
		FREE(bvec->elems);
	} break;
	case tt_dictionary:
	case tt_vector: {
		vector *vec = (vector *)obj;
		size = sizeof(*vec);
		ss->gc.bytes -= vec->cap * sizeof(sly_value);
		FREE(vec->elems);
	} break;
	}
	memset(obj, -1, size);
	ss->gc.bytes -= size;
	FREE(obj);
}

static void
remove_after(Sly_State *ss, gc_object *obj)
{
	gc_object *dead = obj->next;
	obj->next = obj->next->next;
	free_object(ss, dead);
}

static void
remove_beginning(Sly_State *ss)
{
	gc_object *dead = ss->gc.objects;
	ss->gc.objects = ss->gc.objects->next;
	free_object(ss, dead);
}

static void
mark_all_white(Sly_State *ss)
{
	gc_object *obj = ss->gc.objects;
	while (obj) {
		mark_object(obj, GC_WHITE);
		obj = obj->next;
	}
}

static void
sweep(Sly_State *ss)
{
	while (ss->gc.objects
		   && ss->gc.objects->color == GC_WHITE) {
		remove_beginning(ss);
	}
	gc_object *obj = ss->gc.objects;
	if (obj) {
		mark_object(obj, GC_WHITE);
	}
	while (obj && obj->next) {
		if (obj->next->color == GC_WHITE) {
			remove_after(ss, obj);
		}
		obj = obj->next;
	}
}

void
gc_collect(Sly_State *ss)
{
	GC *gc = &ss->gc;
	gc->collections++;
	mark_roots(ss);
	while (gc->grays) {
		propagate_mark(ss);
	}
	sweep(ss);
	mark_all_white(ss);
	gc->treshold = gc->bytes + (gc->bytes / 2);
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
