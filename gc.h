#ifndef SLY_GC_H_
#define SLY_GC_H_

typedef struct _gch {
	uintptr_t prev;
	uintptr_t next;
} gc_header;

typedef struct _gc {
	gc_header *objects;
	size_t obj_count;   /* objects allocated */
	size_t bytes;       /* total bytes allocated */
	size_t tb;          /* threashold bytes */
	int nocollect;
} GC;

struct _sly_state;

void gc_init(GC *gc);
void *gc_alloc(struct _sly_state *ss, size_t bytes, int flags);
void gc_free(GC *gc, void *ptr);
void gc_free_all(struct _sly_state *ss);

#define GC_MARK       0x1
#define GC_PERSISTANT 0x2
#define GC_OWNER      0x4
//#define GC_THRESHOLD ((1 << 10) * 50) /* 50 KiB */
#define GC_THRESHOLD ((1 << 10) * 10) /* 10 KiB */
#define GC_FLAG_MASK 0x7

#endif /* SLY_GC_H_ */
