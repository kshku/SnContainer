# SnContainer

General-purpose container library written in C. Provides efficient, type-safe
data structures backed by user-supplied or default allocators.

## Containers

| Container | Description |
|-----------|-------------|
| DArray | Dynamic array with type-safe push/pop/push_at/pop_at, automatic resizing, and alignment support |
| Sparse Set | Set of values keyed by `uint64_t` ids with O(1) contains/insert/remove/clear and iteration over only the live elements |

## Usage

### DArray

```c
#include <sncontainer/darray.h>
#include <stdio.h>

int main(void) {
    int *numbers = sn_darray_create(int, NULL);
    if (!numbers) return 1;

    sn_darray_push(&numbers, 10);
    sn_darray_push(&numbers, 20);
    sn_darray_push(&numbers, 30);

    for (uint64_t i = 0; i < sn_darray_get_length(numbers); ++i)
        printf("%d\n", numbers[i]);

    sn_darray_destroy(numbers);
    return 0;
}
```

### Sparse Set

```c
#include <sncontainer/sparse_set.h>
#include <stdio.h>

int main(void) {
    int *scores = sn_sparse_set_create(int, NULL);
    if (!scores) return 1;

    /* the ids are supplied by the caller and are expected to be dense */
    sn_sparse_set_insert(&scores, 0, 100);
    sn_sparse_set_insert(&scores, 1, 250);
    sn_sparse_set_insert(&scores, 2, 175);

    /* an element is updated in place through its pointer */
    *sn_sparse_set_at(scores, 1) = 300;

    /* only the live elements are iterated */
    for (uint64_t i = 0; i < sn_sparse_set_get_length(scores); ++i)
        printf("entity %llu: %d\n", (unsigned long long)sn_sparse_set_id_at(scores, i), scores[i]);

    /* remove is O(1), the last element is swapped into the freed slot */
    sn_sparse_set_remove(scores, 1);

    /* clear is O(1) and keeps the capacity */
    sn_sparse_set_clear(scores);

    sn_sparse_set_destroy(scores);
    return 0;
}
```

## Adding to your project

```cmake
include(FetchContent)
FetchContent_Declare(sncontainer
    GIT_REPOSITORY https://github.com/kshku/SnContainer.git
    GIT_TAG <tag>  # e.g., v0.3.0
)
FetchContent_MakeAvailable(sncontainer)

target_link_libraries(myapp PRIVATE sncontainer)
```

## Build

```sh
cmake -B build
cmake --build build
```

| Option | Default | Description |
|--------|---------|-------------|
| `SN_CONTAINER_BUILD_SHARED` | `OFF` | Build as shared library |
| `SN_CONTAINER_BUILD_TEST` | `OFF` | Build tests |

## Notes

- The darray grows by a factor of 2 when full, but **never shrinks on pop**. The capacity
  remains at the highest size reached. If memory is a concern, you can manually call
  `sn_darray_resize` to shrink.
- None of the containers are thread-safe; external synchronization is assumed.
- Darray does not call destructors on popped or cleared elements — it only adjusts
  internal length metadata.
- The sparse set expects **dense ids** (entity handle like). Its sparse array is sized to
  the highest id ever inserted, so a single large id costs memory proportional to that id.
- The sparse set iterates in insertion order modified by removals, not in id order, and
  the order is not stable across removals.
- The sparse set never shrinks. `sn_sparse_set_clear` is O(1) and keeps the capacity, and
  elements past the length should be considered garbage values.
- `sn_sparse_set_insert` and `sn_sparse_set_reserve` may reallocate, so they take the set
  by address and invalidate the set pointer. A pointer returned by `sn_sparse_set_at` is
  invalidated by them as well.
- `sn_sparse_set_insert` asserts if the id is already in the set. Update an existing
  element through `sn_sparse_set_at` instead.
- The allocators must not return NULL. The containers do not handle out of memory, supply
  an `SnMemoryAllocator` wrapper if the application needs to handle it.

## Dependencies

- **SnCore** — fetched automatically via FetchContent
