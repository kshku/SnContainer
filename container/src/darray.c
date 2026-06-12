#include "sncontainer/darray.h"

#include <sncore/utils.h>
#include <string.h>

#define HEADER_SIZE (sizeof(uint64_t) * SN_DARRAY_MAX_FIELDS)

#define ALIGN_BYTE(ptr) ((void *)((uint64_t)(ptr) - 1))
#define GET_ALIGN_SHIFT(ptr) (sn_read_from_bytes(ALIGN_BYTE(ptr), true))
#define SET_ALIGN_SHIFT(ptr, shift) (sn_write_to_bytes(ALIGN_BYTE(ptr), (shift), true))
#define GET_ALIGNED_NEXT(x, align) ((((uint64_t)(x)) + (align)) & ~((align) - 1))

void *impl_sn_darray_create(uint64_t capacity, uint64_t stride, uint64_t align, SnMemoryAllocator *allocator) {
    if (!allocator) allocator = &sn_std_allocator;
    uint64_t total = (capacity * stride) + HEADER_SIZE + align;
    uint64_t *ptr = (uint64_t *)allocator->alloc(allocator->data, total, alignof(uint64_t));
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
    uint64_t ptr = (uint64_t)arr;
    ptr -= GET_ALIGN_SHIFT(ptr);
    ptr -= HEADER_SIZE;
    uint64_t *p = (uint64_t *)ptr;
    SnMemoryAllocator *allocator = (SnMemoryAllocator *)p[SN_DARRAY_ALLOCATOR];
    allocator->free(allocator->data, (void *)ptr);
}

void impl_sn_darray_resize(void **parr, uint64_t capacity) {
    uint64_t ptr = (uint64_t)(*parr);
    uint64_t align_shift = GET_ALIGN_SHIFT(ptr);
    ptr -= align_shift;
    ptr -= HEADER_SIZE;

    uint64_t *p = (uint64_t *)ptr;

    SnMemoryAllocator *allocator = (SnMemoryAllocator *)p[SN_DARRAY_ALLOCATOR];
    p = (uint64_t *)allocator->realloc(
        allocator->data, p, (capacity * p[SN_DARRAY_STRIDE]) + HEADER_SIZE + p[SN_DARRAY_ALIGN],
        alignof(uint64_t));

    p[SN_DARRAY_CAPACITY] = capacity;

    ptr = (uint64_t)p;
    ptr += HEADER_SIZE;
    ptr += align_shift;

    *parr = (void *)ptr;
}

uint64_t impl_sn_darray_header(void *arr, SnDArrayHeader header) {
    uint64_t ptr = (uint64_t)arr;
    ptr -= GET_ALIGN_SHIFT(ptr);
    ptr -= HEADER_SIZE;
    return ((uint64_t *)ptr)[header];
}

void impl_sn_darray_push(void **parr, void *element) {
    uint64_t ptr = (uint64_t)(*parr);
    uint64_t align_shift = GET_ALIGN_SHIFT(ptr);
    ptr -= align_shift;
    ptr -= HEADER_SIZE;
    uint64_t *p = (uint64_t *)ptr;

    if (p[SN_DARRAY_CAPACITY] <= p[SN_DARRAY_SIZE]) {
        sn_darray_resize(parr, p[SN_DARRAY_CAPACITY] * SN_DARRAY_RESIZE_FACTOR);
        ptr = (uint64_t)(*parr);
        ptr -= align_shift;
        ptr -= HEADER_SIZE;
        p = (uint64_t *)ptr;
    }

    void *dest = ((uint8_t *)*parr) + (p[SN_DARRAY_SIZE] * p[SN_DARRAY_STRIDE]);
    memcpy(dest, element, p[SN_DARRAY_STRIDE]);
    ++p[SN_DARRAY_SIZE];
}

void impl_sn_darray_push_at(void **parr, uint64_t index, void *element) {
    uint64_t ptr = (uint64_t)(*parr);
    uint64_t align_shift = GET_ALIGN_SHIFT(ptr);
    ptr -= align_shift;
    ptr -= HEADER_SIZE;
    uint64_t *p = (uint64_t *)ptr;

    if (index > p[SN_DARRAY_SIZE]) {
        if (p[SN_DARRAY_CAPACITY] <= index + 1) {
            sn_darray_resize(parr, index + 1);
            ptr = (uint64_t)(*parr);
            ptr -= align_shift;
            ptr -= HEADER_SIZE;
            p = (uint64_t *)ptr;
        }

        memset(((uint8_t *)*parr) + (p[SN_DARRAY_SIZE] * p[SN_DARRAY_STRIDE]), 0,
               (index - p[SN_DARRAY_SIZE] * p[SN_DARRAY_STRIDE]));

        void *dest = ((uint8_t *)*parr) + (index * p[SN_DARRAY_STRIDE]);
        memcpy(dest, element, p[SN_DARRAY_STRIDE]);

        p[SN_DARRAY_SIZE] = index + 1;

        return;
    }

    if (p[SN_DARRAY_CAPACITY] <= p[SN_DARRAY_SIZE]) {
        sn_darray_resize(parr, p[SN_DARRAY_CAPACITY] * SN_DARRAY_RESIZE_FACTOR);
        ptr = (uint64_t)(*parr);
        ptr -= align_shift;
        ptr -= HEADER_SIZE;
        p = (uint64_t *)ptr;
    }

    void *dest = (uint8_t *)(*parr) + (p[SN_DARRAY_STRIDE] * (index + 1));
    void *src = (uint8_t *)(*parr) + (p[SN_DARRAY_STRIDE] * (index));
    uint64_t size = p[SN_DARRAY_STRIDE] * (p[SN_DARRAY_SIZE] - index);
    memmove(dest, src, size);

    dest = (uint8_t *)(*parr) + (index * p[SN_DARRAY_STRIDE]);
    memcpy(dest, element, p[SN_DARRAY_STRIDE]);

    ++p[SN_DARRAY_SIZE];
}

void impl_sn_darray_pop(void **parr, void *element) {
    uint64_t ptr = (uint64_t)(*parr);
    ptr -= GET_ALIGN_SHIFT(ptr);
    ptr -= HEADER_SIZE;
    uint64_t *p = (uint64_t *)ptr;

    if (p[SN_DARRAY_SIZE] == 0) return;

    --p[SN_DARRAY_SIZE];

    if (element) {
        void *src = ((uint8_t *)*parr) + (p[SN_DARRAY_SIZE] * p[SN_DARRAY_STRIDE]);
        memcpy(element, src, p[SN_DARRAY_STRIDE]);
    }
}

void impl_sn_darray_pop_at(void **parr, uint64_t index, void *element) {
    uint64_t ptr = (uint64_t)(*parr);
    uint64_t align_shift = GET_ALIGN_SHIFT(ptr);
    ptr -= align_shift;
    ptr -= HEADER_SIZE;
    uint64_t *p = (uint64_t *)ptr;

    SN_ASSERT(index < p[SN_DARRAY_SIZE] && "index out of bound while poping element");

    void *dest = ((uint8_t *)*parr) + (index * p[SN_DARRAY_STRIDE]);
    void *src = ((uint8_t *)*parr) + ((index + 1) * p[SN_DARRAY_STRIDE]);
    uint64_t size = (p[SN_DARRAY_SIZE] - index - 1) * p[SN_DARRAY_STRIDE];

    if (element) memcpy(element, dest, p[SN_DARRAY_STRIDE]);
    memmove(dest, src, size);

    --p[SN_DARRAY_SIZE];
}

void impl_sn_darray_clear(void **parr) {
    uint64_t ptr = (uint64_t)(*parr);
    ptr -= GET_ALIGN_SHIFT(ptr);
    ptr -= HEADER_SIZE;
    uint64_t *p = (uint64_t *)ptr;
    p[SN_DARRAY_SIZE] = 0;
}

