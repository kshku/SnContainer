#include "sncontainer/sparse_set.h"

#include <sncore/utils.h>
#include <string.h>

void impl_sn_sparse_set_init(
    SnSparseSet *set, uint64_t capacity, uint64_t stride, void *dense, SnMemoryAllocator *allocator) {
    if (!allocator) allocator = &sn_std_allocator;
    if (capacity == 0) capacity = 1;

    set->dense = dense;
    set->ids = sn_darray_create_with_capacity(capacity, uint64_t, allocator);
    set->sparse = sn_darray_create_with_capacity(capacity, uint64_t, allocator);
    set->stride = stride;

    uint64_t absent = SN_SPARSE_SET_ABSENT;
    for (uint64_t i = 0; i < capacity; ++i) sn_darray_push(&set->sparse, absent);
}

static void grow_sparse(void **psparse, uint64_t index) {
    uint64_t length = sn_darray_get_length(*psparse);
    if (index < length) return;

    uint64_t absent = SN_SPARSE_SET_ABSENT;
    sn_darray_push_at(psparse, index, absent);

    memset((uint64_t *)*psparse + length, 0xFF, (index + 1 - length) * sizeof(uint64_t));
}

bool sn_sparse_set_contains(const SnSparseSet *set, uint64_t key) {
    uint64_t index = key & SN_SPARSE_SET_INDEX_MASK;
    if (index >= sn_darray_get_length(set->sparse)) return false;

    uint64_t slot = ((uint64_t *)set->sparse)[index];
    if (slot == SN_SPARSE_SET_ABSENT) return false;
    if (slot >= sn_darray_get_length(set->ids)) return false;

    return ((uint64_t *)set->ids)[slot] == key;
}

void impl_sn_sparse_set_insert_key(SnSparseSet *set, uint64_t key) {
    uint64_t index = key & SN_SPARSE_SET_INDEX_MASK;
    if (index >= sn_darray_get_length(set->sparse)) grow_sparse(&set->sparse, index);

    SN_ASSERT(((uint64_t *)set->sparse)[index] == SN_SPARSE_SET_ABSENT && "key is already in the set");

    uint64_t slot = sn_darray_get_length(set->ids);
    sn_darray_push(&set->ids, key);
    ((uint64_t *)set->sparse)[index] = slot;
}

void *sn_sparse_set_at(const SnSparseSet *set, uint64_t key) {
    if (!sn_sparse_set_contains(set, key)) return NULL;

    uint64_t slot = ((uint64_t *)set->sparse)[key & SN_SPARSE_SET_INDEX_MASK];

    return (uint8_t *)set->dense + (slot * set->stride);
}

bool sn_sparse_set_get(const SnSparseSet *set, uint64_t key, void *element) {
    void *value = sn_sparse_set_at(set, key);
    if (!value) return false;

    memcpy(element, value, set->stride);
    return true;
}

bool sn_sparse_set_remove(SnSparseSet *set, uint64_t key) {
    if (!sn_sparse_set_contains(set, key)) return false;

    uint64_t *sparse = (uint64_t *)set->sparse;
    uint64_t *ids = (uint64_t *)set->ids;

    uint64_t index = key & SN_SPARSE_SET_INDEX_MASK;
    uint64_t slot = sparse[index];
    uint64_t last = sn_darray_get_length(set->ids) - 1;
    uint64_t last_key = ids[last];

    if (slot != last) {
        memcpy((uint8_t *)set->dense + (slot * set->stride),
               (uint8_t *)set->dense + (last * set->stride), set->stride);
        ids[slot] = last_key;
        sparse[last_key & SN_SPARSE_SET_INDEX_MASK] = slot;
    }

    sparse[index] = SN_SPARSE_SET_ABSENT;
    sn_darray_pop(&set->ids, NULL);
    sn_darray_pop(&set->dense, NULL);

    return true;
}
