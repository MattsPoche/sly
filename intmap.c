#include <gc.h>
#include <string.h>
#include "common_def.h"
#include "sly_types.h"
#include "intmap.h"

#define U32_LMB 0x80000000u

intmap *
intmap_empty(void)
{
	intmap *map = GC_MALLOC(sizeof(intmap));
	memset(map, 0, sizeof(intmap));
	return map;
}

intmap *
intmap_copy(intmap *map)
{
	if (map == NULL) return NULL;
	intmap *new_map = GC_MALLOC(sizeof(intmap));
	new_map->value = map->value;
	for (size_t i = 0; i < INTMAP_NODEC; ++i) {
		new_map->nodes[i] = intmap_copy(map->nodes[i]);
	}
	return new_map;
}

void *
intmap_ref(intmap *map, u32 key)
{
	u32 lb = U32_LMB;
	int i = 0;
	while (key) {
		i = __builtin_clz(key);
		key ^= (lb >> i);
		if (map->nodes[i] == NULL) {
			return NULL;
		}
		map = map->nodes[i];
	}
	return map->value;
}

intmap *
intmap_set_inplace(intmap *map, u32 key, void *value)
{
	u32 lb = U32_LMB;
	int i = 0;
	intmap *cmap = map;
	while (key) {
		i = __builtin_clz(key);
		key ^= (lb >> i);
		if (cmap->nodes[i] == NULL) {
			cmap->nodes[i] = intmap_empty();
		}
		cmap = cmap->nodes[i];
	}
	if (cmap->value) {
		return NULL;
	}
	cmap->value = value;
	return map;
}

intmap *
intmap_set(intmap *map, u32 key, void *value)
{
	u32 lb = U32_LMB;
	int i = 0;
	intmap *new_map = intmap_copy(map);
	intmap *cmap = new_map;
	while (key) {
		i = __builtin_clz(key);
		key ^= (lb >> i);
		if (cmap->nodes[i] == NULL) {
			cmap->nodes[i] = intmap_empty();
		}
		cmap = cmap->nodes[i];
	}
	if (cmap->value) {
		return NULL;
	}
	cmap->value = value;
	return new_map;
}

intmap *
intmap_replace(intmap *map, u32 key, void *value)
{
	u32 lb = U32_LMB;
	int i = 0;
	intmap *new_map = intmap_copy(map);
	intmap *cmap = new_map;
	while (key) {
		i = __builtin_clz(key);
		key ^= (lb >> i);
		if (cmap->nodes[i] == NULL) {
			return NULL;
		}
		cmap = cmap->nodes[i];
	}
	if (cmap->value == NULL) {
		return NULL;
	}
	cmap->value = value;
	return new_map;
}

intmap *
intmap_remove(intmap *map, u32 key)
{
	u32 lb = U32_LMB;
	int i = 0;
	intmap *new_map = intmap_copy(map);
	intmap *cmap = new_map;
	while (key) {
		i = __builtin_clz(key);
		key ^= (lb >> i);
		if (cmap->nodes[i] == NULL) {
			return NULL;
		}
		cmap = cmap->nodes[i];
	}
	if (cmap->value) {
		cmap->value = NULL;
		return new_map;
	}
	return NULL;
}

int
intmap_eqv(intmap *m1, intmap *m2)
{
	if (m1 == m2) return 1;
	if (m1 == NULL || m2 == NULL) return 0;
	for (size_t i = 0; i < INTMAP_NODEC; ++i) {
		if (!intmap_eqv(m1->nodes[i], m2->nodes[i])) {
			return 0;
		}
	}
	return m1->value == m2->value;
}

intmap_list *
intmap_list_node(u32 key, void *value)
{
	intmap_list *node = GC_MALLOC(sizeof(*node));
	node->p.key = key;
	node->p.value = value;
	node->next = NULL;
	return node;
}

intmap_list *
intmap_list_append(intmap_list *xs, intmap_list *ys)
{
	if (xs == NULL) {
		return ys;
	}
	if (ys == NULL) {
		return xs;
	}
	intmap_list *tmp = xs;
	while (tmp->next) {
		tmp = tmp->next;
	}
	tmp->next = ys;
	return xs;
}

void
intmap_foreach(intmap *imap, u32 key, intmap_iter_cb cb, void *ud)
{
	struct intmap_kv_pair p;
	if (imap->value) {
		p.value = imap->value;
		p.key = key;
		cb(p, ud);
	}
	for (int i = 0; i < INTMAP_NODEC; ++i) {
		if (imap->nodes[i]) {
			intmap_foreach(imap, key | (U32_LMB >> i), cb, ud);
		}
	}
}

static intmap_list *
build_list(intmap *imap, u32 key)
{
	intmap_list *list = NULL;
	if (imap->value) {
		list = intmap_list_node(key, imap->value);
	}
	for (int i = 0; i < INTMAP_NODEC; ++i) {
		if (imap->nodes[i]) {
			list = intmap_list_append(list, build_list(imap->nodes[i], key | (U32_LMB >> i)));
		}
	}
	return list;
}

intmap_list *
intmap_to_list(intmap *imap)
{
	return build_list(imap, 0);
}

int
intmap_list_member(intmap_list *imap, u32 key)
{
	while (imap) {
		if (imap->p.key == key) {
			return 1;
		}
		imap = imap->next;
	}
	return 0;
}

intmap *
intmap_union(intmap *m1, intmap *m2)
{
	if (intmap_eqv(m1, m2)) {
		return m1;
	}
	intmap *new_map = intmap_copy(m1);
	intmap_list *list = intmap_to_list(m2);
	while (list) {
		intmap_set_inplace(new_map, list->p.key, list->p.value);
		list = list->next;
	}
	return new_map;
}

intmap *
intmap_intersect(intmap *m1, intmap *m2)
{
	intmap *new_map = intmap_empty();
	intmap_list *list = intmap_to_list(m1);
	void *value;
	while (list) {
		value = intmap_ref(m2, list->p.key);
		if (value == list->p.value) {
			intmap_set_inplace(new_map, list->p.key, value);
		}
		list = list->next;
	}
	return new_map;
}
