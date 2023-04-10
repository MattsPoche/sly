#ifndef SLY_ALLOC_H_
#define SLY_ALLOC_H_

#ifdef USE_SLY_ALLOC

void *sly_alloc(size_t size);
void sly_free(void *ptr);

#define MALLOC sly_alloc
#define FREE   sly_free

#else

#define MALLOC malloc
#define FREE   free

#endif /* USE_SLY_ALLOC */
#endif /* SLY_ALLOC_H_ */
