#ifndef INTMAP_H_
#define INTMAP_H_

#define INTMAP_NODEC 32

typedef struct _intmap {
	void *value;
	struct _intmap *nodes[INTMAP_NODEC];
} intmap;

struct intmap_kv_pair {
	void *value;
	u32 key;
};

typedef struct _intmap_list {
	struct _intmap_list *next;
	struct intmap_kv_pair p;
} intmap_list;

typedef void *(*intmap_iter_cb)(struct intmap_kv_pair, void *);

intmap *intmap_empty(void);
intmap *intmap_copy(intmap *map);
void *intmap_ref(intmap *map, u32 key);
intmap *intmap_set_inplace(intmap *map, u32 key, void *value);
intmap *intmap_set(intmap *map, u32 key, void *value);
intmap *intmap_replace(intmap *map, u32 key, void *value);
intmap *intmap_remove(intmap *map, u32 key);
int intmap_eqv(intmap *m1, intmap *m2);
intmap_list *intmap_list_node(u32 key, void *value);
intmap_list *intmap_to_list(intmap *imap);
int intmap_list_member(intmap_list *imap, u32 key);
intmap_list *intmap_list_append(intmap_list *xs, intmap_list *ys);
intmap *intmap_union(intmap *m1, intmap *m2);
intmap *intmap_intersect(intmap *m1, intmap *m2);
intmap *intmap_subtract(intmap *m1, intmap *m2);

#endif /* INTMAP_H_ */
