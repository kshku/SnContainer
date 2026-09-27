#pragma once

#include "sncontainer/api.h"
#include "sncontainer/darray.h"

#include <sncore/defines.h>
#include <sncore/types.h>

/**
 * @defgroup KeyAllocator Versioned Key Allocator
 * @brief Mints and recycles generational uint64_t keys.
 *
 * A key allocator hands out opaque uint64_t keys and recycles them when they are
 * released. Each key packs a 32 bit index into a 32 bit generation:
 *
 * @code
 * key = (generation << 32) | index
 * @endcode
 *
 * The index identifies the slot, the generation counts how many times that slot
 * has been handed out. Releasing a key makes it invalid immediately, and the next
 * acquire of the same index returns a key with a higher generation. A caller that
 * still holds the old key can therefore detect that it went stale in O(1) with
 * sn_key_allocator_is_valid, instead of silently reading whatever now occupies
 * the slot.
 *
 * This is a general purpose versioned handle allocator, not an ECS specific type.
 * It is the natural companion to a keyed container such as a sparse set
 * (@ref SparseSet), but it works on its own and knows nothing about any
 * container. Many keyed containers can share one allocator, in which case a
 * single key addresses the same logical entity in all of them.
 *
 * @note
 * - The allocator is owned by the caller. Declare it as a local or embed it in a
 *   larger struct, sn_key_allocator_init prepares it and sn_key_allocator_deinit
 *   releases only the arrays it manages. Nothing is allocated for the struct
 *   itself.
 * - Generations start at 1, so key 0 is never handed out and is never valid.
 * - A generation overflows back to 0 after 2^32 recycles of the same index, which
 *   would make a very old key valid again. That needs roughly four billion reuses
 *   of one index and is only guarded by a debug assert.
 * - A released key is only invalid in the allocator. Any container that still
 *   holds it keeps reporting it until it is removed from that container, so
 *   remove a key from every container before releasing it.
 * - The allocator never shrinks, keys released once keep their memory reserved.
 * - Not thread-safe.
 * - The allocator must not return NULL. Out of memory is not handled by the
 *   container, supply a SnMemoryAllocator wrapper if the application needs to
 *   handle it.
 * @{
 */

#define SN_KEY_ALLOCATOR_DEFAULT_CAPACITY 5

/** Mask of the index half of a key. */
#define SN_KEY_ALLOCATOR_INDEX_MASK UINT64_C(0xFFFFFFFF)

/** Amount added to a key to move it to the next generation. */
#define SN_KEY_ALLOCATOR_GENERATION_STEP (UINT64_C(1) << 32)

/** Generation of a key the first time its index is handed out. */
#define SN_KEY_ALLOCATOR_FIRST_GENERATION UINT64_C(1)

/** Largest generation an index can reach before it overflows. */
#define SN_KEY_ALLOCATOR_MAX_GENERATION UINT64_C(0xFFFFFFFF)

/**
 * @brief State of a key allocator.
 *
 * The allocator is meant to be owned by the caller: declare it as a local, embed
 * it in a larger struct, or place it wherever it needs to live. It only owns its
 * two internal arrays, which sn_key_allocator_init creates and
 * sn_key_allocator_deinit releases.
 *
 * free_keys holds released keys in full, so that the generation of a recycled
 * index travels with it. generations holds the current generation of every index
 * ever handed out, indexed by the index half of a key, with 0 meaning the index
 * is currently free. That is what makes sn_key_allocator_is_valid O(1).
 */
typedef struct SnKeyAllocator {
    void *free_keys; /**< uint64_t array of released keys awaiting reuse */
    void *generations; /**< uint32_t array of the current generation per index, 0 if free */
    uint64_t next_index; /**< The next index that was never handed out */
    uint64_t live_count; /**< Number of keys currently issued */
} SnKeyAllocator;

/**
 * @brief Initialize a key allocator.
 *
 * The allocator is owned by the caller, so nothing is allocated for the struct
 * itself, only for the arrays it manages. Every key allocator must be initialized
 * before use and deinitialized when it is no longer needed.
 *
 * @code
 * SnKeyAllocator keys;
 * sn_key_allocator_init(&keys, NULL);
 * // ...
 * sn_key_allocator_deinit(&keys);
 * @endcode
 *
 * @param key_allocator The key allocator to initialize
 * @param allocator The allocator to use for the internal arrays (if NULL the standard
 * library's allocator is used (malloc family functions))
 */
SN_CONTAINER_API void sn_key_allocator_init(SnKeyAllocator *key_allocator, SnMemoryAllocator *allocator);

/**
 * @brief Release the arrays of a key allocator.
 *
 * Keys issued by it must not be used afterwards. The struct itself is not freed,
 * the caller owns it.
 *
 * @param key_allocator The key allocator
 */
#define sn_key_allocator_deinit(key_allocator)           \
    do {                                                 \
        sn_darray_destroy((key_allocator)->free_keys);   \
        sn_darray_destroy((key_allocator)->generations); \
    } while (0)

/**
 * @brief Acquire a key.
 *
 * Reuses the index of a released key when one is available, in which case the
 * generation is bumped so the previous holder of that key can tell it went
 * stale, otherwise takes the next index never handed out with generation
 * SN_KEY_ALLOCATOR_FIRST_GENERATION. The returned key is never 0.
 *
 * @param key_allocator The key allocator
 *
 * @return The acquired key.
 */
SN_CONTAINER_API uint64_t sn_key_allocator_acquire(SnKeyAllocator *key_allocator);

/**
 * @brief Release a key so its index can be reused.
 *
 * The key becomes invalid immediately. A later acquire that recycles its index
 * returns a key with a higher generation, so this key never becomes valid again.
 *
 * @note Remove the key from every container that holds it before releasing it,
 * those containers keep reporting it as present until it is removed.
 *
 * @param key_allocator The key allocator
 * @param key The key to release
 *
 * @return true if the key was valid and got released, false if it was not valid.
 */
SN_CONTAINER_API bool sn_key_allocator_release(SnKeyAllocator *key_allocator, uint64_t key);

/**
 * @brief Check whether a key is currently issued.
 *
 * Returns false for a key that was never acquired, for a released key, and for
 * a key whose index has since been recycled at a higher generation.
 *
 * @param key_allocator The key allocator
 * @param key The key
 *
 * @return true if the key is currently issued, false otherwise.
 */
SN_CONTAINER_API bool sn_key_allocator_is_valid(SnKeyAllocator *key_allocator, uint64_t key);

/**
 * @brief Get the number of keys currently issued.
 *
 * @param key_allocator The key allocator
 *
 * @return The number of live keys.
 */
#define sn_key_allocator_get_live_count(key_allocator) ((key_allocator)->live_count)

/**
 * @brief Get the number of released keys waiting to be reused.
 *
 * @param key_allocator The key allocator
 *
 * @return The number of free keys.
 */
#define sn_key_allocator_get_free_count(key_allocator) \
    sn_darray_get_length((key_allocator)->free_keys)

/**
 * @brief Get the number of indices the allocator has ever handed out.
 *
 * This is the high water mark of the index space and only grows.
 *
 * @param key_allocator The key allocator
 *
 * @return The number of indices ever used.
 */
#define sn_key_allocator_get_next_index(key_allocator) ((key_allocator)->next_index)

/** @} */
