#include "sncontainer/sparse_set.h"

#include <sncore/utils.h>
#include <string.h>

#define HEADER_SIZE (sizeof(uint64_t) * SN_SPARSE_SET_MAX_FIELDS)

#define ALIGN_BYTE(ptr) ((void *)((uint64_t)(ptr) - 1))
#define GET_ALIGN_SHIFT(ptr) (sn_read_from_bytes(ALIGN_BYTE(ptr), true))
#define SET_ALIGN_SHIFT(ptr, shift) (sn_write_to_bytes(ALIGN_BYTE(ptr), (shift), true))
#define GET_ALIGNED_NEXT(x, align) ((((uint64_t)(x)) + (align)) & ~((align) - 1))

static uint64_t *get_header(void *set) {
    uint64_t ptr = (uint64_t)set;
    ptr -= GET_ALIGN_SHIFT(ptr);
    ptr -= HEADER_SIZE;
    return (uint64_t *)ptr;
}

static void grow_sparse(void **pset, uint64_t min_capacity) {
    uint64_t *p = get_header(*pset);
    SnMemoryAllocator *allocator = (SnMemoryAllocator *)p[SN_SPARSE_SET_ALLOCATOR];
    uint64_t old_capacity = p[SN_SPARSE_SET_SPARSE_CAPACITY];
    uint64_t new_capacity = SN_MAX(old_capacity * SN_SPARSE_SET_RESIZE_FACTOR, min_capacity);

    uint64_t *sparse = allocator->realloc(
        allocator->data, (void *)p[SN_SPARSE_SET_SPARSE], new_capacity * sizeof(uint64_t), alignof(uint64_t));
    memset(sparse + old_capacity, 0, (new_capacity - old_capacity) * sizeof(uint64_t));

    p[SN_SPARSE_SET_SPARSE] = (uint64_t)sparse;
    p[SN_SPARSE_SET_SPARSE_CAPACITY] = new_capacity;
}

static void grow_dense(void **pset, uint64_t min_capacity) {
    uint64_t *p = get_header(*pset);
    SnMemoryAllocator *allocator = (SnMemoryAllocator *)p[SN_SPARSE_SET_ALLOCATOR];
    uint64_t stride = p[SN_SPARSE_SET_STRIDE];
    uint64_t align = p[SN_SPARSE_SET_ALIGN];
    uint64_t old_capacity = p[SN_SPARSE_SET_CAPACITY];
    uint64_t new_capacity = SN_MAX(old_capacity * SN_SPARSE_SET_RESIZE_FACTOR, min_capacity);

    uint64_t base = (uint64_t)(*pset) - GET_ALIGN_SHIFT(*pset) - HEADER_SIZE;
    uint64_t *np = allocator->realloc(
        allocator->data, (void *)base, (new_capacity * stride) + HEADER_SIZE + align, alignof(uint64_t));
    np[SN_SPARSE_SET_CAPACITY] = new_capacity;

    /* The reallocated block can be at a different offset, so the shift to the
     * aligned element array has to be recomputed. */
    uint64_t data = GET_ALIGNED_NEXT((uint64_t)np + HEADER_SIZE, align);
    SET_ALIGN_SHIFT((void *)data, data - ((uint64_t)np + HEADER_SIZE));
    *pset = (void *)data;

    uint64_t *ids = allocator->realloc(
        allocator->data, (void *)np[SN_SPARSE_SET_IDS], new_capacity * sizeof(uint64_t), alignof(uint64_t));
    np[SN_SPARSE_SET_IDS] = (uint64_t)ids;
}

void *impl_sn_sparse_set_create(uint64_t capacity, uint64_t stride, uint64_t align, SnMemoryAllocator *allocator) {
    if (!allocator) allocator = &sn_std_allocator;
    uint64_t total = (capacity * stride) + HEADER_SIZE + align;
    uint64_t *ptr = (uint64_t *)allocator->alloc(allocator->data, total, alignof(uint64_t));
    memset(ptr, 0, total);
    ptr[SN_SPARSE_SET_CAPACITY] = capacity;
    ptr[SN_SPARSE_SET_COUNT] = 0;
    ptr[SN_SPARSE_SET_STRIDE] = stride;
    ptr[SN_SPARSE_SET_ALIGN] = align;
    ptr[SN_SPARSE_SET_SPARSE_CAPACITY] = SN_SPARSE_SET_DEFAULT_CAPACITY;
    ptr[SN_SPARSE_SET_ALLOCATOR] = (uint64_t)allocator;

    uint64_t *sparse = (uint64_t *)allocator->alloc(
        allocator->data, SN_SPARSE_SET_DEFAULT_CAPACITY * sizeof(uint64_t), alignof(uint64_t));
    memset(sparse, 0, SN_SPARSE_SET_DEFAULT_CAPACITY * sizeof(uint64_t));
    ptr[SN_SPARSE_SET_SPARSE] = (uint64_t)sparse;

    uint64_t *ids
        = (uint64_t *)allocator->alloc(allocator->data, capacity * sizeof(uint64_t), alignof(uint64_t));
    memset(ids, 0, capacity * sizeof(uint64_t));
    ptr[SN_SPARSE_SET_IDS] = (uint64_t)ids;

    uint64_t aligned = GET_ALIGNED_NEXT((uint64_t)ptr + HEADER_SIZE, align);
    SET_ALIGN_SHIFT((void *)aligned, aligned - ((uint64_t)ptr + HEADER_SIZE));

    return (void *)aligned;
}

void impl_sn_sparse_set_destroy(void *set) {
    uint64_t *p = get_header(set);
    SnMemoryAllocator *allocator = (SnMemoryAllocator *)p[SN_SPARSE_SET_ALLOCATOR];
    uint64_t base = (uint64_t)set - GET_ALIGN_SHIFT(set) - HEADER_SIZE;

    allocator->free(allocator->data, (void *)p[SN_SPARSE_SET_SPARSE]);
    allocator->free(allocator->data, (void *)p[SN_SPARSE_SET_IDS]);
    allocator->free(allocator->data, (void *)base);
}

void impl_sn_sparse_set_reserve(void **pset, uint64_t capacity, uint64_t max_id) {
    SN_ASSERT(max_id != UINT64_MAX && "max_id must be smaller than UINT64_MAX");

    if (capacity > get_header(*pset)[SN_SPARSE_SET_CAPACITY]) grow_dense(pset, capacity);
    if (max_id + 1 > get_header(*pset)[SN_SPARSE_SET_SPARSE_CAPACITY])
        grow_sparse(pset, max_id + 1);
}

uint64_t impl_sn_sparse_set_header(void *set, SnSparseSetHeader header) {
    return get_header(set)[header];
}

bool impl_sn_sparse_set_contains(void *set, uint64_t id) {
    uint64_t *p = get_header(set);
    if (id >= p[SN_SPARSE_SET_SPARSE_CAPACITY]) return false;

    uint64_t index = ((uint64_t *)p[SN_SPARSE_SET_SPARSE])[id];
    if (index >= p[SN_SPARSE_SET_COUNT]) return false;

    return ((uint64_t *)p[SN_SPARSE_SET_IDS])[index] == id;
}

void impl_sn_sparse_set_insert(void **pset, uint64_t id, void *element) {
    SN_ASSERT(!impl_sn_sparse_set_contains(*pset, id) && "id already present in the sparse set");
    SN_ASSERT(id != UINT64_MAX && "id must be smaller than UINT64_MAX");

    if (id >= get_header(*pset)[SN_SPARSE_SET_SPARSE_CAPACITY]) grow_sparse(pset, id + 1);

    uint64_t *p = get_header(*pset);
    if (p[SN_SPARSE_SET_COUNT] == p[SN_SPARSE_SET_CAPACITY]) {
        grow_dense(pset, 0);
        p = get_header(*pset);
    }

    uint64_t index = p[SN_SPARSE_SET_COUNT];
    memcpy((uint8_t *)(*pset) + (index * p[SN_SPARSE_SET_STRIDE]), element, p[SN_SPARSE_SET_STRIDE]);
    ((uint64_t *)p[SN_SPARSE_SET_IDS])[index] = id;
    ((uint64_t *)p[SN_SPARSE_SET_SPARSE])[id] = index;
    ++p[SN_SPARSE_SET_COUNT];
}

void *impl_sn_sparse_set_at(void *set, uint64_t id) {
    uint64_t *p = get_header(set);
    if (!impl_sn_sparse_set_contains(set, id)) return NULL;

    uint64_t index = ((uint64_t *)p[SN_SPARSE_SET_SPARSE])[id];
    return (uint8_t *)set + (index * p[SN_SPARSE_SET_STRIDE]);
}

bool impl_sn_sparse_set_get(void *set, uint64_t id, void *element) {
    void *value = impl_sn_sparse_set_at(set, id);
    if (!value) return false;

    memcpy(element, value, get_header(set)[SN_SPARSE_SET_STRIDE]);
    return true;
}

bool impl_sn_sparse_set_remove(void *set, uint64_t id) {
    if (!impl_sn_sparse_set_contains(set, id)) return false;

    uint64_t *p = get_header(set);
    uint64_t *sparse = (uint64_t *)p[SN_SPARSE_SET_SPARSE];
    uint64_t *ids = (uint64_t *)p[SN_SPARSE_SET_IDS];
    uint64_t stride = p[SN_SPARSE_SET_STRIDE];

    uint64_t index = sparse[id];
    uint64_t last = --p[SN_SPARSE_SET_COUNT];

    if (index != last) {
        memcpy((uint8_t *)set + (index * stride), (uint8_t *)set + (last * stride), stride);
        ids[index] = ids[last];
        sparse[ids[index]] = index;
    }

    return true;
}

void impl_sn_sparse_set_clear(void *set) {
    get_header(set)[SN_SPARSE_SET_COUNT] = 0;
}

uint64_t impl_sn_sparse_set_id_at(void *set, uint64_t index) {
    uint64_t *p = get_header(set);

    SN_ASSERT(index < p[SN_SPARSE_SET_COUNT] && "index out of bound while getting id");

    return ((uint64_t *)p[SN_SPARSE_SET_IDS])[index];
}
