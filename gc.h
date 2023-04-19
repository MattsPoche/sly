#ifndef SLY_GC_H_
#define SLY_GC_H_

typedef struct _gc {
	struct _gc_object *objects;
	struct _gc_object *grays;
	size_t bytes;       /* bytes allocated */
	size_t treshold;
	int collections;
} GC;

struct _sly_state;

void gc_init(GC *gc);
void *gc_alloc(struct _sly_state *ss, size_t size);
void gc_collect(struct _sly_state *ss);
void gc_free_all(struct _sly_state *ss);

#define GARBAGE_COLLECT(ss)							\
	do {											\
		if ((ss)->gc.bytes > (ss)->gc.treshold) {	\
			gc_collect(ss);							\
		}											\
	} while (0)

#endif /* SLY_GC_H_ */
