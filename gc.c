#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common_def.h"
#include "gc.h"

#define BYTES_TO_CELLS(bytes) \
	(((bytes) / sizeof(gccell)) + (((bytes) % sizeof(gccell)) ? 1 : 0))

static void mp_resize(struct mem_pool *pool, size_t bytes);
static void mp_init(struct mem_pool *pool, size_t bytes);
static void *mp_alloc(struct mem_pool *pool, size_t bytes);

static void
mp_resize(struct mem_pool *pool, size_t bytes)
{
	pool->cap = BYTES_TO_CELLS(bytes);
	void *ptr = realloc(pool->cells, pool->cap * sizeof(gccell));
	assert(ptr != NULL);
	if ((uintptr_t)pool->cells != (uintptr_t)ptr) {
		printf("(mp_resize) looks sus, man\n");
	}
	pool->cells = ptr;
}

static void
mp_init(struct mem_pool *pool, size_t bytes)
{
	pool->free = 0;
	pool->cap = BYTES_TO_CELLS(bytes);
	pool->cells = malloc(pool->cap * sizeof(gccell));
	assert(pool->cells != NULL);
}

static void *
mp_alloc(struct mem_pool *pool, size_t bytes)
{
	size_t cc = BYTES_TO_CELLS(bytes);
	size_t avail = pool->cap - pool->free;
	if (cc > avail) {
		return NULL;
	}
	void *ptr = &pool->cells[pool->free];
	pool->free += cc;
	return ptr;
}

void
gc_init(GC *gc)
{
	mp_init(&gc->w, GC_INIT_CAP);
	mp_init(&gc->f, GC_INIT_CAP);
}

void *
gc_alloc(GC *gc, size_t bytes, int setinfo)
{
	void *ptr = mp_alloc(&gc->w, bytes);
	/* TODO garbage collect/resize when alloc fails */
	assert(ptr != NULL);
	(void)mp_resize;
	if (setinfo) {
		gcinfo *info = ptr;
		info->len = BYTES_TO_CELLS(bytes);
	}
	return ptr;
}
