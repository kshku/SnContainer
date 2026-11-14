#include "sncontainer/sparse_set.h"

#include <sncore/utils.h>
#include <snmemory/std_allocator.h>
#include <string.h>

bool impl_sn_sparse_set_init(
    SnSparseSet *set, uint64_t capacity, uint64_t stride, void *dense, SnMemoryAllocator *allocator) {
    if (!allocator) allocator = &sn_std_allocator;
    if (capacity == 0) capacity = 1;

    set->dense = dense;
    set->ids = NULL;
    set->sparse = NULL;
    set->stride = stride;

    if (!dense) return false;

    set->ids = sn_darray_create_with_capacity(capacity, uint64_t, allocator);
    if (!set->ids) {
        sn_darray_destroy(set->dense);
        set->dense = NULL;
        return false;
    }

    set->sparse = sn_darray_create_with_capacity(capacity, uint64_t, allocator);
    if (!set->sparse) {
        sn_darray_destroy(set->ids);
        sn_darray_destroy(set->dense);
        set->dense = NULL;
        set->ids = NULL;
        return false;
    }

    uint64_t absent = SN_SPARSE_SET_ABSENT;
    for (uint64_t i = 0; i < capacity; ++i) {
        if (!sn_darray_push(&set->sparse, absent)) {
            sn_darray_destroy(set->sparse);
            sn_darray_destroy(set->ids);
            sn_darray_destroy(set->dense);
            set->dense = NULL;
            set->ids = NULL;
            set->sparse = NULL;
            return false;
        }
    }

    return true;
}

static bool grow_sparse(void **psparse, uint64_t index) {
    uint64_t length = sn_darray_get_length(*psparse);
    if (index < length) return true;

    uint64_t absent = SN_SPARSE_SET_ABSENT;
    if (!sn_darray_push_at(psparse, index, absent)) return false;

    memset((uint64_t *)*psparse + length, 0xFF, (index + 1 - length) * sizeof(uint64_t));
    return true;
}

bool sn_sparse_set_contains(const SnSparseSet *set, uint64_t key) {
    uint64_t index = key & SN_SPARSE_SET_INDEX_MASK;
    if (index >= sn_darray_get_length(set->sparse)) return false;

    uint64_t slot = ((uint64_t *)set->sparse)[index];
    if (slot == SN_SPARSE_SET_ABSENT) return false;
    if (slot >= sn_darray_get_length(set->ids)) return false;

    return ((uint64_t *)set->ids)[slot] == key;
}

bool impl_sn_sparse_set_insert_key(SnSparseSet *set, uint64_t key) {
    uint64_t index = key & SN_SPARSE_SET_INDEX_MASK;
    if (index >= sn_darray_get_length(set->sparse)) {
        if (!grow_sparse(&set->sparse, index)) return false;
    }

    SN_ASSERT(((uint64_t *)set->sparse)[index] == SN_SPARSE_SET_ABSENT && "key is already in the set");

    uint64_t slot = sn_darray_get_length(set->ids);
    if (!sn_darray_push(&set->ids, key)) return false;

    ((uint64_t *)set->sparse)[index] = slot;
    return true;
}

bool impl_sn_sparse_set_insert(SnSparseSet *set, uint64_t key, void *element) {
    uint64_t index = key & SN_SPARSE_SET_INDEX_MASK;
    if (index >= sn_darray_get_length(set->sparse)) {
        if (!grow_sparse(&set->sparse, index)) return false;
    }

    SN_ASSERT(((uint64_t *)set->sparse)[index] == SN_SPARSE_SET_ABSENT && "key is already in the set");

    /* Reserve both arrays before mutating either, so a failed dense allocation
     * cannot leave ids holding a key with no matching element. */
    uint64_t needed = sn_darray_get_length(set->ids) + 1;
    if (!sn_darray_reserve(&set->ids, needed)) return false;
    if (!sn_darray_reserve(&set->dense, needed)) return false;

    uint64_t slot = sn_darray_get_length(set->ids);
    (void)sn_darray_push(&set->ids, key);
    (void)sn_darray_push_from_ptr(&set->dense, element);
    ((uint64_t *)set->sparse)[index] = slot;

    return true;
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
