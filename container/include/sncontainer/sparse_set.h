#pragma once

#include "sncontainer/api.h"

#include <sncore/defines.h>
#include <sncore/types.h>

/**
 * @defgroup SparseSet Sparse Set
 * @brief Hash-free set of values keyed by dense uint32_t ids.
 *
 * A sparse set stores its elements packed in a dense array and keeps a parallel
 * sparse array that maps an id to the position of its element in that dense array.
 * This gives O(1) contains, insert, remove and clear, and iteration over only the
 * live elements with stable element references.
 *
 * @note
 * - The ids are expected to be dense (entity handle like). The sparse array is
 *   sized to the highest id ever inserted, so a single huge id costs memory
 *   proportional to that id.
 * - Removing an element swaps the last dense element into the freed slot, so the
 *   iteration order is insertion order modified by removals. It is not id order and
 *   is not stable across removals.
 * - The capacity never shrinks. Use destroy and create again to reclaim memory.
 * - Not thread-safe.
 * - The allocator must not return NULL. Out of memory is not handled by the
 *   container, supply a SnMemoryAllocator wrapper if the application needs to
 *   handle it.
 * @{
 */

#define SN_SPARSE_SET_DEFAULT_CAPACITY 5
#define SN_SPARSE_SET_RESIZE_FACTOR 2

SN_STATIC_ASSERT(sizeof(uint64_t) == sizeof(void *), "size of pointer != size of uint64_t");

/**
 * @brief Header fields of a sparse set.
 *
 * The header is stored in front of the dense element array, the first field of the
 * header is the start of the dense array itself.
 */
typedef enum SnSparseSetHeader {
    SN_SPARSE_SET_SPARSE, /**< uint32_t array mapping id to dense index */
    SN_SPARSE_SET_IDS, /**< uint32_t array holding the id of each dense slot */
    SN_SPARSE_SET_CAPACITY, /**< Number of elements the dense array can hold */
    SN_SPARSE_SET_COUNT, /**< Number of live elements */
    SN_SPARSE_SET_STRIDE, /**< Size of an element in bytes */
    SN_SPARSE_SET_ALIGN, /**< Alignment of an element in bytes */
    SN_SPARSE_SET_SPARSE_CAPACITY, /**< Number of ids the sparse array can hold */
    SN_SPARSE_SET_ALLOCATOR, /**< The allocator used by the set */

    SN_SPARSE_SET_MAX_FIELDS,
} SnSparseSetHeader;

/**
 * @brief Create a sparse set with the given element type and capacity.
 *
 * The sparse array is created with SN_SPARSE_SET_DEFAULT_CAPACITY ids and grows as
 * larger ids are inserted.
 *
 * @param capacity The initial capacity (number of elements) of the dense array
 * @param type Type of the element
 * @param allocator The allocator to use (if NULL the standard library's allocator is
 * used (malloc family functions))
 *
 * @return The sparse set or NULL on failure.
 */
#define sn_sparse_set_create_with_capacity(capacity, type, allocator)                       \
    (type *)impl_sn_sparse_set_create((capacity), sizeof(type), alignof(type), (allocator))

/**
 * @brief Create a sparse set with the given element type.
 *
 * @param type Type of the elements
 * @param allocator The allocator to use (if NULL the standard library's allocator is
 * used)
 *
 * @return The sparse set or NULL on failure.
 */
#define sn_sparse_set_create(type, allocator)                                           \
    sn_sparse_set_create_with_capacity(SN_SPARSE_SET_DEFAULT_CAPACITY, type, allocator)

/**
 * @brief Destroy the sparse set.
 *
 * @param set The sparse set
 */
#define sn_sparse_set_destroy(set) impl_sn_sparse_set_destroy(set)

/**
 * @brief Reserve capacity for elements and ids.
 *
 * Grows the dense array to at least the given capacity and the sparse array to at
 * least max_id + 1 ids. Both are hints, neither ever shrinks.
 *
 * @note Invalidates the set pointer, so it must be passed by address.
 *
 * @param pset Pointer to the sparse set
 * @param capacity The number of elements to reserve
 * @param max_id The highest id to reserve room for
 */
#define sn_sparse_set_reserve(pset, capacity, max_id)               \
    impl_sn_sparse_set_reserve((void **)pset, (capacity), (max_id))

/**
 * @brief Get the number of live elements in the sparse set.
 *
 * @param set The sparse set
 *
 * @return The number of elements.
 */
#define sn_sparse_set_get_length(set) impl_sn_sparse_set_header((set), SN_SPARSE_SET_COUNT)

/**
 * @brief Get the current capacity of the sparse set.
 *
 * Returns the number of elements the dense array can hold before it is resized.
 *
 * @param set The sparse set
 *
 * @return The capacity of the set.
 */
#define sn_sparse_set_get_capacity(set) impl_sn_sparse_set_header((set), SN_SPARSE_SET_CAPACITY)

/**
 * @brief Get the number of ids the sparse array of the set can hold.
 *
 * @param set The sparse set
 *
 * @return The capacity of the sparse array.
 */
#define sn_sparse_set_get_sparse_capacity(set)                      \
    impl_sn_sparse_set_header((set), SN_SPARSE_SET_SPARSE_CAPACITY)

/**
 * @brief Check if an id is in the sparse set.
 *
 * @param set The sparse set
 * @param id The id
 *
 * @return true if the id is in the set, false otherwise.
 */
#define sn_sparse_set_contains(set, id) impl_sn_sparse_set_contains((set), (id))

/**
 * @brief Insert an element with the given id.
 *
 * The element is appended to the dense array, so iteration order is insertion order
 * modified by removals.
 *
 * @note Invalidates the set pointer if the set has to grow, so it must be passed by
 * address.
 *
 * @param pset Pointer to the sparse set
 * @param id The id of the element (must be smaller than UINT32_MAX)
 * @param element The element
 */
#define sn_sparse_set_insert(pset, id, element)                                      \
    impl_sn_sparse_set_insert((void **)pset, (id), (__typeof__(element)[]){element})

/**
 * @brief Get a pointer to the element with the given id.
 *
 * The returned pointer can be used to update the element in place. It is
 * invalidated by insert and reserve.
 *
 * @param set The sparse set
 * @param id The id
 *
 * @return Pointer to the element or NULL if the id is not in the set.
 */
#define sn_sparse_set_at(set, id) ((__typeof__(*(set)) *)impl_sn_sparse_set_at((set), (id)))

/**
 * @brief Copy the element with the given id into element.
 *
 * @param set The sparse set
 * @param id The id
 * @param element Pointer to store the element
 *
 * @return true on success, false if the id is not in the set (element is untouched).
 */
#define sn_sparse_set_get(set, id, element) impl_sn_sparse_set_get((set), (id), (element))

/**
 * @brief Remove the element with the given id.
 *
 * The last dense element is swapped into the freed slot.
 *
 * @param set The sparse set
 * @param id The id
 *
 * @return true if the element was removed, false if the id is not in the set.
 */
#define sn_sparse_set_remove(set, id) impl_sn_sparse_set_remove((set), (id))

/**
 * @brief Remove all the elements from the sparse set.
 *
 * Just sets the number of elements to zero, the elements should be considered
 * garbage values. The capacity is preserved.
 *
 * @param set The sparse set
 */
#define sn_sparse_set_clear(set) impl_sn_sparse_set_clear(set)

/**
 * @brief Get the dense element array of the sparse set.
 *
 * The array holds sn_sparse_set_get_length(set) elements, elements past that length
 * are garbage values.
 *
 * @param set The sparse set
 *
 * @return The dense element array.
 */
#define sn_sparse_set_data(set) (set)

/**
 * @brief Get the id of the element at the given index of the dense array.
 *
 * @param set The sparse set
 * @param index The index of the element (must be less than the length of the set)
 *
 * @return The id of the element.
 */
#define sn_sparse_set_id_at(set, index) impl_sn_sparse_set_id_at((set), (index))

/** @} */

SN_CONTAINER_API void *impl_sn_sparse_set_create(
    uint64_t capacity, uint64_t stride, uint64_t align, SnMemoryAllocator *allocator);

SN_CONTAINER_API void impl_sn_sparse_set_destroy(void *set);

SN_CONTAINER_API void impl_sn_sparse_set_reserve(void **pset, uint64_t capacity, uint64_t max_id);

SN_CONTAINER_API uint64_t impl_sn_sparse_set_header(void *set, SnSparseSetHeader header);

SN_CONTAINER_API bool impl_sn_sparse_set_contains(void *set, uint32_t id);

SN_CONTAINER_API void impl_sn_sparse_set_insert(void **pset, uint32_t id, void *element);

SN_CONTAINER_API void *impl_sn_sparse_set_at(void *set, uint32_t id);

SN_CONTAINER_API bool impl_sn_sparse_set_get(void *set, uint32_t id, void *element);

SN_CONTAINER_API bool impl_sn_sparse_set_remove(void *set, uint32_t id);

SN_CONTAINER_API void impl_sn_sparse_set_clear(void *set);

SN_CONTAINER_API uint32_t impl_sn_sparse_set_id_at(void *set, uint64_t index);
