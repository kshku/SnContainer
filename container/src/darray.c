#include "sncontainer/darray.h"

#include <sncore/utils.h>
#include <snmemory/std_allocator.h>
#include <string.h>

#define HEADER_SIZE (sizeof(uint64_t) * SN_DARRAY_MAX_FIELDS)

#define ALIGN_BYTE(ptr) ((void *)((uint64_t)(ptr) - 1))
#define GET_ALIGN_SHIFT(ptr) (sn_read_from_bytes(ALIGN_BYTE(ptr), true))
#define SET_ALIGN_SHIFT(ptr, shift) (sn_write_to_bytes(ALIGN_BYTE(ptr), (shift), true))
#define GET_ALIGNED_NEXT(x, align) ((((uint64_t)(x)) + (align)) & ~((align) - 1))

static uint64_t *darray_header(void *arr) {
    uint64_t ptr = (uint64_t)arr;
    ptr -= GET_ALIGN_SHIFT(ptr);
    ptr -= HEADER_SIZE;
    return (uint64_t *)ptr;
}

/* Grow the array so that it can hold at least `needed` elements. On failure the
 * array is left untouched and false is returned, so the caller can abort without
 * having mutated anything. */
static bool darray_ensure_capacity(void **parr, uint64_t needed, uint64_t **p_out) {
    uint64_t *p = darray_header(*parr);

    if (needed > p[SN_DARRAY_CAPACITY]) {
        uint64_t new_size = p[SN_DARRAY_CAPACITY] * SN_DARRAY_RESIZE_FACTOR;
        while (new_size < needed) new_size *= SN_DARRAY_RESIZE_FACTOR;

        if (!impl_sn_darray_resize(parr, new_size)) return false;
        p = darray_header(*parr);
    }

    *p_out = p;
    return true;
}

void *impl_sn_darray_create(uint64_t capacity, uint64_t stride, uint64_t align, SnMemoryAllocator *allocator) {
    if (!allocator) allocator = &sn_std_allocator;
    if (capacity == 0) capacity = 1;
    uint64_t total = (capacity * stride) + HEADER_SIZE + align;
    uint64_t *ptr = (uint64_t *)allocator->alloc(allocator->data, total, alignof(uint64_t));
    if (!ptr) return NULL;
    memset(ptr, 0, total);
    ptr[SN_DARRAY_CAPACITY] = capacity;
    ptr[SN_DARRAY_SIZE] = 0;
    ptr[SN_DARRAY_STRIDE] = stride;
    ptr[SN_DARRAY_ALIGN] = align;
    ptr[SN_DARRAY_ALLOCATOR] = (uint64_t)allocator;

    ptr = (uint64_t *)((uint64_t)ptr + HEADER_SIZE);

    uint64_t aligned = GET_ALIGNED_NEXT(ptr, align);
    SET_ALIGN_SHIFT(aligned, (aligned - (uint64_t)ptr));

    return (void *)aligned;
}

void impl_sn_darray_destroy(void *arr) {
    if (!arr) return;

    uint64_t *p = darray_header(arr);
    SnMemoryAllocator *allocator = (SnMemoryAllocator *)p[SN_DARRAY_ALLOCATOR];
    allocator->free(allocator->data, (void *)p);
}

bool impl_sn_darray_resize(void **parr, uint64_t capacity) {
    uint64_t size_limit = capacity;
    if (capacity == 0) capacity = 1;

    uint64_t align_shift = GET_ALIGN_SHIFT(*parr);
    uint64_t *p = darray_header(*parr);

    if (capacity == p[SN_DARRAY_CAPACITY]) {
        if (size_limit < p[SN_DARRAY_SIZE]) p[SN_DARRAY_SIZE] = size_limit;
        return true;
    }

    SnMemoryAllocator *allocator = (SnMemoryAllocator *)p[SN_DARRAY_ALLOCATOR];
    uint64_t *new_p = (uint64_t *)allocator->realloc(
        allocator->data, p, (capacity * p[SN_DARRAY_STRIDE]) + HEADER_SIZE + p[SN_DARRAY_ALIGN],
        alignof(uint64_t));
    if (!new_p) return false;

    new_p[SN_DARRAY_CAPACITY] = capacity;
    if (size_limit < new_p[SN_DARRAY_SIZE]) new_p[SN_DARRAY_SIZE] = size_limit;

    *parr = (void *)((uint64_t)new_p + HEADER_SIZE + align_shift);
    return true;
}

bool impl_sn_darray_reserve(void **parr, uint64_t min_capacity) {
    uint64_t *p = darray_header(*parr);
    if (min_capacity <= p[SN_DARRAY_CAPACITY]) return true;

    uint64_t new_size = p[SN_DARRAY_CAPACITY];
    while (new_size < min_capacity) new_size *= SN_DARRAY_RESIZE_FACTOR;

    return impl_sn_darray_resize(parr, new_size);
}

uint64_t impl_sn_darray_header(void *arr, SnDArrayHeader header) {
    return darray_header(arr)[header];
}

bool impl_sn_darray_push_elements(void **parr, void *elements, uint64_t len) {
    if (len == 0) return true;

    uint64_t *p = darray_header(*parr);
    uint64_t needed = p[SN_DARRAY_SIZE] + len;
    if (!darray_ensure_capacity(parr, needed, &p)) return false;

    void *dest = ((uint8_t *)*parr) + (p[SN_DARRAY_SIZE] * p[SN_DARRAY_STRIDE]);
    memcpy(dest, elements, (len * p[SN_DARRAY_STRIDE]));
    p[SN_DARRAY_SIZE] += len;
    return true;
}

bool impl_sn_darray_push_elements_at(void **parr, uint64_t index, void *elements, uint64_t len) {
    if (len == 0) return true;

    uint64_t *p = darray_header(*parr);

    if (index == p[SN_DARRAY_SIZE]) return impl_sn_darray_push_elements(parr, elements, len);

    uint64_t needed = (index > p[SN_DARRAY_SIZE]) ? index + len : p[SN_DARRAY_SIZE] + len;
    if (!darray_ensure_capacity(parr, needed, &p)) return false;

    if (index > p[SN_DARRAY_SIZE]) {
        memset(((uint8_t *)*parr) + (p[SN_DARRAY_SIZE] * p[SN_DARRAY_STRIDE]), 0,
               ((p[SN_DARRAY_CAPACITY] - p[SN_DARRAY_SIZE]) * p[SN_DARRAY_STRIDE]));

        void *dest = ((uint8_t *)*parr) + (index * p[SN_DARRAY_STRIDE]);
        memcpy(dest, elements, (len * p[SN_DARRAY_STRIDE]));

        p[SN_DARRAY_SIZE] = index + len;
    } else {
        void *dest = (uint8_t *)(*parr) + (p[SN_DARRAY_STRIDE] * (index + len));
        void *src = (uint8_t *)(*parr) + (p[SN_DARRAY_STRIDE] * (index));
        uint64_t size = p[SN_DARRAY_STRIDE] * (p[SN_DARRAY_SIZE] - index);
        memmove(dest, src, size);

        dest = (uint8_t *)(*parr) + (index * p[SN_DARRAY_STRIDE]);
        memcpy(dest, elements, (len * p[SN_DARRAY_STRIDE]));

        p[SN_DARRAY_SIZE] += len;
    }

    return true;
}

void impl_sn_darray_pop(void **parr, void *element) {
    uint64_t *p = darray_header(*parr);

    if (p[SN_DARRAY_SIZE] == 0) return;

    --p[SN_DARRAY_SIZE];

    if (element) {
        void *src = ((uint8_t *)*parr) + (p[SN_DARRAY_SIZE] * p[SN_DARRAY_STRIDE]);
        memcpy(element, src, p[SN_DARRAY_STRIDE]);
    }
}

void impl_sn_darray_pop_at(void **parr, uint64_t index, void *element) {
    uint64_t *p = darray_header(*parr);

    SN_ASSERT(index < p[SN_DARRAY_SIZE] && "index out of bound while poping element");

    void *dest = ((uint8_t *)*parr) + (index * p[SN_DARRAY_STRIDE]);
    void *src = ((uint8_t *)*parr) + ((index + 1) * p[SN_DARRAY_STRIDE]);
    uint64_t size = (p[SN_DARRAY_SIZE] - index - 1) * p[SN_DARRAY_STRIDE];

    if (element) memcpy(element, dest, p[SN_DARRAY_STRIDE]);
    memmove(dest, src, size);

    --p[SN_DARRAY_SIZE];
}

void impl_sn_darray_clear(void **parr) {
    darray_header(*parr)[SN_DARRAY_SIZE] = 0;
}

bool impl_sn_darray_shrink_to_fit(void **parr) {
    uint64_t *p = darray_header(*parr);

    uint64_t size = p[SN_DARRAY_SIZE];
    if (size == p[SN_DARRAY_CAPACITY]) return true;

    return impl_sn_darray_resize(parr, size);
}
