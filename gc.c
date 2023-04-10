#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common_def.h"
#include "sly_types.h"
#include "sly_alloc.h"
#include "opcodes.h"

#define GCH_SET_FLAGS(h, flags) ((h)->prev = (((h)->prev & ~GC_FLAG_MASK) | (flags)))
#define GCH_GET_FLAGS(h)        ((h)->prev & GC_FLAG_MASK)
#define GCH_SET_PREV(h, obj) \
	((h)->prev = (((h)->prev & GC_FLAG_MASK) | (uintptr_t)(obj)))
#define GCH_GET_PREV(h)	((gc_header *)((h)->prev & ~GC_FLAG_MASK))
#define GCH_SET_NEXT(h, obj) ((h)->next = (uintptr_t)(obj))
#define GCH_GET_NEXT(h) ((gc_header *)(h)->next)
#define GC_FREE_OBJECT(ptr)											\
	do {															\
		gc_header *_obj = (ptr);									\
		if (GCH_GET_FLAGS(_obj) & GC_OWNER) {						\
			struct obj_header *_h = (ptr);							\
			switch (_h->type) {										\
			case tt_symbol: {										\
				symbol *_s = (ptr);									\
				FREE(_s->name);										\
			} break;												\
			case tt_byte_vector: { __attribute__((fallthrough)); }	\
			case tt_vector: { __attribute__((fallthrough)); }		\
			case tt_string: { __attribute__((fallthrough)); }		\
			case tt_dictionary: {									\
				byte_vector *_v = (ptr);							\
				FREE(_v->elems);									\
			} break;												\
			}														\
		}															\
		FREE(ptr);													\
	} while (0)

#define GCH_SET_MARK(h)							\
	do {										\
		int flags = GCH_GET_FLAGS(h);			\
		GCH_SET_FLAGS(h, flags & GC_MARK);		\
	} while (0)

static void gc_mark(Sly_State *ss, sly_value obj);
static void gc_sweep(GC *gc);

void
gc_init(GC *gc)
{
	memset(gc, 0, sizeof(*gc));
}

void *
gc_alloc(struct _sly_state *ss, size_t size, int flags)
{
	GC *gc = &ss->gc;
	gc_header *ptr = NULL;
	if (size) {
		ptr = MALLOC(size);
		ptr->prev = 0;
		ptr->next = 0;
		GCH_SET_FLAGS(ptr, flags);
		if (gc->objects) {
			GCH_SET_PREV(gc->objects, ptr);
			GCH_SET_NEXT(ptr, gc->objects);
		}
		gc->objects = ptr;
		gc->obj_count++;
		gc->bytes += size;
		gc->tb += size;
	}
	if (!gc->nocollect && gc->tb > GC_THRESHOLD) {
		gc->tb = 0;
		gc_mark(ss, (sly_value)ss->frame);
		gc_sweep(gc);
	}
	return ptr;
}

void
gc_free_all(struct _sly_state *ss)
{
	GC *gc = &ss->gc;
	gc_header *obj = gc->objects;
	gc_header *nxt;
	size_t count = gc->obj_count;
	while (count--) {
		nxt = GCH_GET_NEXT(obj);
		GC_FREE_OBJECT((void *)obj);
		obj = nxt;
	}
	gc->objects = NULL;
	gc->obj_count = 0;
}

void
gc_free(GC *gc, void *ptr)
{
	gc_header *obj = ptr;
	gc_header *prev = GCH_GET_PREV(obj);
	gc_header *next = GCH_GET_NEXT(obj);
	if (prev) GCH_SET_NEXT(prev, next);
	if (next) GCH_SET_PREV(next, prev);
	GC_FREE_OBJECT(ptr);
	gc->obj_count--;
}

static void
gc_mark(Sly_State *ss, sly_value obj)
{ /* TODO: Implement this */
	if (!heap_obj_p(obj)) {
		return;
	}
	gc_header *hdr = GET_PTR(obj);
	if (GCH_GET_FLAGS(hdr) & GC_MARK) {
		return;
	}
	GCH_SET_MARK(hdr);
	if (pair_p(obj)) {
		gc_mark(ss, car(obj));
		gc_mark(ss, cdr(obj));
		return;
	}
	struct obj_header *obj_hdr = GET_PTR(obj);
	switch ((enum type_tag)obj_hdr->type) {
	case tt_byte:
	case tt_int:
	case tt_float:
	case tt_symbol:
	case tt_string:
	case tt_byte_vector: break;
	case tt_dictionary:
	case tt_vector: {
		vector *vec = GET_PTR(obj);
		for (size_t i = 0; i < vec->len; ++i) {
			gc_mark(ss, vec->elems[i]);
		}
	} break;
	case tt_prototype: {
		prototype *proto = GET_PTR(obj);
		gc_mark(ss, proto->uplist);
		gc_mark(ss, proto->K);
		/* mark code but don't try to mark instructions */
		hdr = GET_PTR(proto->code);
		GCH_SET_MARK(hdr);
	} break;
	case tt_closure: {
		closure *clos = GET_PTR(obj);
		gc_mark(ss, clos->upvals);
		gc_mark(ss, clos->proto);
	} break;
	case tt_cclosure: break;
	case tt_continuation: {
		continuation *cont = GET_PTR(obj);
		gc_mark(ss, (sly_value)cont->frame);
	} break;
	case tt_syntax: {
		syntax *syn = GET_PTR(obj);
		gc_mark(ss, syn->datum);
	} break;
	case tt_stack_frame: {
		stack_frame *frame = GET_PTR(obj);
		gc_mark(ss, (sly_value)frame->parent);
		gc_mark(ss, frame->K);
		/* mark code but don't try to mark instructions */
		hdr = GET_PTR(frame->code);
		GCH_SET_MARK(hdr);
		gc_mark(ss, frame->U);
		gc_mark(ss, frame->R);
		gc_mark(ss, frame->clos);
	} break;
	case TYPE_TAG_COUNT: {
		sly_assert(0, "(gc_mark) Invalid type TYPE_TAG_COUNT");
	} break;
	}
}

static void
gc_sweep(GC *gc)
{ /* TODO fix this */
	if (gc->objects == NULL) return;
	gc_header _head = {0};
	gc_header *head = &_head;
	GCH_SET_NEXT(head, gc->objects);
	GCH_SET_PREV(gc->objects, head);
	gc_header *obj = gc->objects;
	int flags = 0;
	while (obj) {
		if ((flags = GCH_GET_FLAGS(obj))) {
			flags ^= GC_MARK;
			GCH_SET_FLAGS(obj, flags);
			obj = GCH_GET_NEXT(obj);
		} else {
			gc_header *next = GCH_GET_NEXT(obj);
			gc_free(gc, obj);
			obj = next;
		}
	}
	gc->objects = GCH_GET_NEXT(head);
}
