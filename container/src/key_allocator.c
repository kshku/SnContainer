#include "sncontainer/key_allocator.h"

#include <sncore/utils.h>
#include <snmemory/std_allocator.h>

bool sn_key_allocator_init(SnKeyAllocator *key_allocator, SnMemoryAllocator *allocator) {
    if (!allocator) allocator = &sn_std_allocator;

    key_allocator->free_keys = NULL;
    key_allocator->generations = NULL;
    key_allocator->next_index = 0;
    key_allocator->live_count = 0;

    key_allocator->free_keys
        = sn_darray_create_with_capacity(SN_KEY_ALLOCATOR_DEFAULT_CAPACITY, uint64_t, allocator);
    if (!key_allocator->free_keys) return false;

    key_allocator->generations
        = sn_darray_create_with_capacity(SN_KEY_ALLOCATOR_DEFAULT_CAPACITY, uint32_t, allocator);
    if (!key_allocator->generations) {
        sn_darray_destroy(key_allocator->free_keys);
        key_allocator->free_keys = NULL;
        return false;
    }

    return true;
}

static void set_generation(SnKeyAllocator *key_allocator, uint64_t index, uint32_t generation) {
    uint64_t length = sn_darray_get_length(key_allocator->generations);

    while (length <= index) {
        uint32_t free_index = 0;
        (void)sn_darray_push(&key_allocator->generations, free_index);
        ++length;
    }

    ((uint32_t *)key_allocator->generations)[index] = generation;
}

uint64_t sn_key_allocator_acquire(SnKeyAllocator *key_allocator) {
    uint64_t free_count = sn_darray_get_length(key_allocator->free_keys);
    bool recycled = free_count > 0;
    uint64_t key;

    if (recycled) {
        uint64_t last = free_count - 1;
        key = ((uint64_t *)key_allocator->free_keys)[last];
        SN_ASSERT(((key >> 32) < SN_KEY_ALLOCATOR_MAX_GENERATION) && "key generation exhausted");
        key += SN_KEY_ALLOCATOR_GENERATION_STEP;
    } else {
        SN_ASSERT((key_allocator->next_index <= SN_KEY_ALLOCATOR_INDEX_MASK) && "key index space exhausted");
        key = (SN_KEY_ALLOCATOR_FIRST_GENERATION << 32) | key_allocator->next_index;
    }

    uint64_t index = key & SN_KEY_ALLOCATOR_INDEX_MASK;

    /* Make room before touching any state, so a failed allocation leaves the
     * free list and the index counter exactly as they were. */
    if (!sn_darray_reserve(&key_allocator->generations, index + 1)) return 0;

    if (recycled) {
        sn_darray_pop(&key_allocator->free_keys, NULL);
    } else {
        ++key_allocator->next_index;
    }

    set_generation(key_allocator, index, (uint32_t)(key >> 32));
    ++key_allocator->live_count;

    return key;
}

bool sn_key_allocator_release(SnKeyAllocator *key_allocator, uint64_t key) {
    if (!sn_key_allocator_is_valid(key_allocator, key)) return false;

    /* Push before invalidating, so a failed allocation leaves the key valid. */
    if (!sn_darray_push(&key_allocator->free_keys, key)) return false;

    uint64_t index = key & SN_KEY_ALLOCATOR_INDEX_MASK;
    ((uint32_t *)key_allocator->generations)[index] = 0;
    --key_allocator->live_count;

    return true;
}

bool sn_key_allocator_is_valid(SnKeyAllocator *key_allocator, uint64_t key) {
    uint64_t generation = key >> 32;
    if (generation == 0) return false;

    uint64_t index = key & SN_KEY_ALLOCATOR_INDEX_MASK;
    if (index >= sn_darray_get_length(key_allocator->generations)) return false;

    return ((uint32_t *)key_allocator->generations)[index] == (uint32_t)generation;
}
