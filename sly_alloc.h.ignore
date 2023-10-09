#ifndef SLY_ALLOC_H_
#define SLY_ALLOC_H_


#ifdef USE_SLY_ALLOC

extern int allocations;
extern int net_allocations;
extern size_t bytes_allocated;

void *sly_alloc(size_t size);
void sly_free(void *ptr);

#define MALLOC sly_alloc
#define FREE   sly_free

#else

#define MALLOC malloc
#define FREE   free

#endif /* USE_SLY_ALLOC */
#endif /* SLY_ALLOC_H_ */
