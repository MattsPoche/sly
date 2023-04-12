#ifndef SLY_GC_H_
#define SLY_GC_H_

typedef struct _gc {
	struct _gc_object *objects;
	struct _gc_object *grays;
	size_t obj_count;
	size_t obj_freed;
	size_t tb;          /* threashold bytes */
	int nocollect;
} GC;

struct _sly_state;

#define GC_GRAY 0
#define GC_WHITE 1
#define GC_BLACK 2
#define GC_THRESHOLD ((1 << 10) * 10) /* 10 KiB */

void gc_init(GC *gc);
void *gc_alloc(struct _sly_state *ss, size_t size);
void gc_collect(struct _sly_state *ss);
void gc_free_all(struct _sly_state *ss);

#endif /* SLY_GC_H_ */
