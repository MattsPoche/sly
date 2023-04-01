#ifndef SLY_GC_H_
#define SLY_GC_H_

typedef uintptr_t gccell;

typedef struct _gci {
	u8 mark;    /* marked if all bits are set */
	u8 flags;
	u16 len;    /* length of chunk in cells */
} gcinfo;

struct mem_pool {
	size_t free;    /* next free cell */
	size_t cap;      /* cell capacity */
	gccell *cells;
};

typedef struct _gc {
	struct mem_pool w;  /* working memory */
	struct mem_pool f;  /* free memory */
} GC;

void gc_init(GC *gc);
void *gc_alloc(GC *gc, size_t bytes, int setinfo);

#define GC_INIT_CAP 1024 * 10000
#define MARK_VALUE 0xff

#endif /* SLY_GC_H_ */
