#pragma once

#include "sncontainer/api.h"
#include "sncontainer/darray.h"

#include <sncore/defines.h>
#include <sncore/types.h>

/**
 * @defgroup SparseSet Sparse Set
 * @brief Hash-free set of values keyed by uint64_t keys.
 *
 * A sparse set stores its elements packed in a dense array with no holes, and
 * keeps two parallel arrays alongside it: an ids array holding the key of each
 * dense slot, and a sparse array mapping the index half of a key to the slot its
 * element currently occupies. This gives O(1) contains, insert, remove and clear,
 * plus iteration over only the live elements.
 *
 * A key packs a 32 bit index and a 32 bit generation:
 *
 * @code
 * key = (generation << 32) | index
 * @endcode
 *
 * The sparse set never mints, recycles or bumps keys, so it accepts any uint64_t
 * key. Pair it with a key allocator (@ref KeyAllocator) when keys should be
 * recycled and stale keys detected in O(1), or supply your own keys when the index
 * space is already yours.
 *
 * Because the lookup compares all 64 bits, a key whose index has been reused for
 * a different element is rejected even though it shares the index half. The index
 * half locates a slot, it does not identify an element.
 *
 * @note
 * - The set is owned by the caller. Declare it as a local or embed it in a larger
 *   struct, sn_sparse_set_init prepares it and sn_sparse_set_deinit releases only
 *   the arrays it manages. Nothing is allocated for the struct itself.
 * - The sparse array is indexed directly by the index half of a key, so it costs
 *   8 bytes for every index it can address, whether or not that index is
 *   occupied, and it never shrinks. This is the dominant memory cost of the
 *   structure. With keys recycled by a key allocator the indices stay close to
 *   the peak number of live elements, so the cost is roughly 8 bytes times that
 *   peak, per set. Many component sets multiply that.
 * - The sparse array grows lazily when a key with a larger index arrives, so one
 *   stray large key costs memory proportional to that key: an index of 5e9 asks
 *   for a 40GB sparse array. Keep keys dense when memory matters.
 * - Removing an element swaps the last dense element into the freed slot, so the
 *   iteration order is insertion order modified by removals. It is not key order
 *   and is not stable across removals.
 * - The capacity never shrinks, on the dense array or the sparse array. Use
 *   sn_sparse_set_deinit and init again to reclaim memory.
 * - Elements past the length should be considered garbage values, no destructors
 *   are called for removed or cleared elements.
 * - A pointer returned by sn_sparse_set_at is invalidated by an insert that grows
 *   the dense array. The set itself is never invalidated.
 * - Not thread-safe.
 * - The allocator must not return NULL. Out of memory is not handled by the
 *   container, supply a SnMemoryAllocator wrapper if the application needs to
 *   handle it.
 * @{
 */

#define SN_SPARSE_SET_DEFAULT_CAPACITY 5

/** Marker stored in the sparse array for an index that is not in the set. */
#define SN_SPARSE_SET_ABSENT UINT64_MAX

/** Mask of the index half of a key. */
#define SN_SPARSE_SET_INDEX_MASK UINT64_C(0xFFFFFFFF)

SN_STATIC_ASSERT(SN_SPARSE_SET_ABSENT == UINT64_MAX, "sparse array fill relies on the absent "
                                                     "marker being all ones");

/**
 * @brief State of a sparse set.
 *
 * The three arrays are dynamic arrays (@ref DArray), so they grow on their own and
 * are reached through their handle. dense and ids are parallel, the same slot index
 * addresses the payload in one and the key in the other. sparse is indexed by the
 * index half of a key and holds the slot, or SN_SPARSE_SET_ABSENT when that index
 * is not in the set.
 */
typedef struct SnSparseSet {
    void *dense; /**< The packed payload array */
    void *ids; /**< uint64_t array holding the key of each dense slot */
    void *sparse; /**< uint64_t array mapping the index half of a key to a dense slot */
    uint64_t stride; /**< Size of an element in bytes */
} SnSparseSet;

/**
 * @brief Initialize a sparse set with the given element type and capacity.
 *
 * The set is owned by the caller, so nothing is allocated for the struct itself,
 * only for the arrays it manages. Every sparse set must be initialized before use
 * and deinitialized when it is no longer needed.
 *
 * The dense, ids and sparse arrays are created with the given capacity. The dense
 * and ids arrays grow automatically as elements are inserted, the sparse array
 * grows as keys with larger indices arrive. None of them ever shrink.
 *
 * @code
 * SnSparseSet scores;
 * sn_sparse_set_init(&scores, Score, NULL);
 * // ...
 * sn_sparse_set_deinit(&scores);
 * @endcode
 *
 * @param set The sparse set to initialize
 * @param capacity The initial capacity (number of elements) of the set
 * @param type Type of the element
 * @param allocator The allocator to use (if NULL the standard library's allocator is
 * used (malloc family functions))
 */
#define sn_sparse_set_init_with_capacity(pset, capacity, type, allocator) \
    impl_sn_sparse_set_init((pset), (capacity), sizeof(type),             \
                            sn_darray_create_with_capacity((capacity), type, (allocator)), (allocator))

/**
 * @brief Initialize a sparse set with the given element type.
 *
 * @param set The sparse set to initialize
 * @param type Type of the elements
 * @param allocator The allocator to use (if NULL the standard library's allocator is
 * used)
 */
#define sn_sparse_set_init(pset, type, allocator)                                             \
    sn_sparse_set_init_with_capacity((pset), SN_SPARSE_SET_DEFAULT_CAPACITY, type, allocator)

/**
 * @brief Insert an element with the given key.
 *
 * The element is appended to the dense array, so the iteration order is insertion
 * order modified by removals. The key must not already be in the set, inserting a
 * duplicate is a programming error caught by a debug assert.
 *
 * @note The sparse array grows to reach the index half of the key if needed. A
 * large index costs 8 bytes of sparse array per index up to it, see the group
 * notes.
 *
 * @note The element is taken as a macro argument, so a braced initializer needs
 * a name of its own, a compound literal would be read as extra arguments.
 *
 * @param set The sparse set
 * @param key The key of the element
 * @param element The element
 */
#define sn_sparse_set_insert(pset, key, element)      \
    do {                                              \
        impl_sn_sparse_set_insert_key((pset), (key)); \
        sn_darray_push(&(pset)->dense, (element));    \
    } while (0)

/**
 * @brief Insert an element pointed by given pointer with the given key.
 *
 * @param set The sparse set
 * @param key The key of the element
 * @param pelement Pointer to the element
 */
#define sn_sparse_set_insert_from_ptr(pset, key, pelement)   \
    do {                                                     \
        impl_sn_sparse_set_insert_key((pset), (key));        \
        sn_darray_push_from_ptr(&(pset)->dense, (pelement)); \
    } while (0)

/**
 * @brief Release the arrays of a sparse set.
 *
 * The struct itself is not freed, the caller owns it.
 *
 * @param set The sparse set
 */
#define sn_sparse_set_deinit(pset)         \
    do {                                   \
        sn_darray_destroy((pset)->dense);  \
        sn_darray_destroy((pset)->ids);    \
        sn_darray_destroy((pset)->sparse); \
    } while (0)

/**
 * @brief Get the number of live elements in the sparse set.
 *
 * @param set The sparse set
 *
 * @return The number of elements.
 */
#define sn_sparse_set_get_length(pset) sn_darray_get_length((pset)->ids)

/**
 * @brief Get the current capacity of the dense array of the set.
 *
 * Returns the number of elements the dense array can hold before it is resized.
 *
 * @param set The sparse set
 *
 * @return The capacity of the dense array.
 */
#define sn_sparse_set_get_capacity(pset) sn_darray_get_capacity((pset)->dense)

/**
 * @brief Get the number of indices the sparse array of the set can hold.
 *
 * This is the memory the set spends on lookups, 8 bytes per index it can address.
 * It only grows, and because the array grows by doubling it can be larger than the
 * range of indices actually seen.
 *
 * @param set The sparse set
 *
 * @return The capacity of the sparse array.
 */
#define sn_sparse_set_get_sparse_capacity(pset) sn_darray_get_capacity((pset)->sparse)

/**
 * @brief Check whether a key is in the sparse set.
 *
 * Compares all 64 bits of the key, so a key whose index was reused for another
 * element is reported as absent.
 *
 * @param set The sparse set
 * @param key The key
 *
 * @return true if the key is in the set, false otherwise.
 */
SN_CONTAINER_API bool sn_sparse_set_contains(const SnSparseSet *set, uint64_t key);

/**
 * @brief Get a pointer to the element with the given key.
 *
 * The returned pointer can be used to update the element in place. It is
 * invalidated by an insert that grows the dense array.
 *
 * @param set The sparse set
 * @param key The key
 *
 * @return Pointer to the element or NULL if the key is not in the set.
 */
SN_CONTAINER_API void *sn_sparse_set_at(const SnSparseSet *set, uint64_t key);

/**
 * @brief Copy the element with the given key into element.
 *
 * @param set The sparse set
 * @param key The key
 * @param element Pointer to store the element
 *
 * @return true on success, false if the key is not in the set (element is untouched).
 */
SN_CONTAINER_API bool sn_sparse_set_get(const SnSparseSet *set, uint64_t key, void *element);

/**
 * @brief Remove the element with the given key.
 *
 * The last dense element is swapped into the freed slot, which keeps the dense
 * array packed and makes removal O(1) at the cost of disturbing the iteration
 * order. The index of the removed key is marked absent, so it is reported as
 * missing and can be inserted again.
 *
 * @param set The sparse set
 * @param key The key
 *
 * @return true if the element was removed, false if the key is not in the set.
 */
SN_CONTAINER_API bool sn_sparse_set_remove(SnSparseSet *set, uint64_t key);

/**
 * @brief Remove all the elements from the sparse set.
 *
 * Just sets the number of elements to zero, the elements should be considered
 * garbage values. The sparse array is emptied as well, so no stale entry survives
 * a clear, and it is filled with absent markers again as keys arrive. The
 * capacities are preserved, so clearing is O(1) and gives no memory back.
 *
 * @param set The sparse set
 */
#define sn_sparse_set_clear(pset)         \
    do {                                  \
        sn_darray_clear(&(pset)->dense);  \
        sn_darray_clear(&(pset)->ids);    \
        sn_darray_clear(&(pset)->sparse); \
    } while (0)

/**
 * @brief Get the key of the element at the given slot of the dense array.
 *
 * Iterating slots from 0 to sn_sparse_set_get_length(set) visits every live element
 * exactly once.
 *
 * @param set The sparse set
 * @param slot The slot of the element (must be less than the length of the set)
 *
 * @return The key of the element.
 */
#define sn_sparse_set_key_at(pset, slot) (((uint64_t *)(pset)->ids)[(slot)])

SN_CONTAINER_API void impl_sn_sparse_set_init(
    SnSparseSet *set, uint64_t capacity, uint64_t stride, void *dense, SnMemoryAllocator *allocator);

SN_CONTAINER_API void impl_sn_sparse_set_insert_key(SnSparseSet *set, uint64_t key);

/** @} */
