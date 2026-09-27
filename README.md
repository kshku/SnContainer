# SnContainer

General-purpose container library written in C. Provides efficient, type-safe
data structures backed by user-supplied or default allocators.

## Containers

| Container | Description |
|-----------|-------------|
| DArray | Dynamic array with type-safe push/pop/push_at/pop_at, automatic resizing, and alignment support |
| Key Allocator | Hands out generational `uint64_t` keys, recycles the index of a released key and reports a stale key in O(1) |
| Sparse Set | Set of values keyed by `uint64_t` keys with O(1) contains/insert/remove/clear and iteration over only the live elements |

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

### Key Allocator

A key packs a 32 bit index and a 32 bit generation, `(generation << 32) | index`.
Generations start at 1, so a key is never 0. Releasing a key invalidates it at
once and the next acquire of that index returns the index with a higher
generation, which is how a holder of the old key detects that it went stale.

```c
#include <sncontainer/key_allocator.h>

SnKeyAllocator keys;
sn_key_allocator_init(&keys, NULL);

uint64_t key = sn_key_allocator_acquire(&keys);   /* never 0 */
sn_key_allocator_release(&keys, key);             /* invalid immediately */

uint64_t again = sn_key_allocator_acquire(&keys); /* same index, higher generation */
sn_key_allocator_is_valid(&keys, key);            /* false, the key went stale */
sn_key_allocator_is_valid(&keys, again);          /* true */

sn_key_allocator_deinit(&keys);
```

The allocator is a general purpose versioned handle allocator. It knows nothing
about any container, so any number of keyed containers can share one allocator
and a single key then addresses the same logical entity in all of them.

### Sparse Set

A sparse set stores its elements packed in a dense array, so iteration visits
only the live elements. It is keyed by an arbitrary `uint64_t`, it never mints
keys, so pair it with a key allocator when keys should be recycled.

```c
#include <sncontainer/key_allocator.h>
#include <sncontainer/sparse_set.h>
#include <stdio.h>

typedef struct { double x, y; } Position;

int main(void) {
    SnKeyAllocator keys;
    SnSparseSet positions;
    sn_key_allocator_init(&keys, NULL);
    sn_sparse_set_init(&positions, Position, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);
    uint64_t b = sn_key_allocator_acquire(&keys);

    /* a braced initializer needs a name, a macro would read it as extra arguments */
    Position first = {1.0, 2.0};
    Position second = {3.0, 4.0};
    sn_sparse_set_insert(&positions, a, first);
    sn_sparse_set_insert(&positions, b, second);

    /* an element is updated in place through its pointer */
    ((Position *)sn_sparse_set_at(&positions, a))->x = 5.0;

    /* only the live elements are iterated */
    for (uint64_t i = 0; i < sn_sparse_set_get_length(&positions); ++i) {
        uint64_t key = sn_sparse_set_key_at(&positions, i);
        Position *p = sn_sparse_set_at(&positions, key);
        printf("key %llu: %.1f %.1f\n", (unsigned long long)key, p->x, p->y);
    }

    /* remove is O(1), the last element is swapped into the freed slot */
    sn_sparse_set_remove(&positions, a);

    /* a key goes stale in the allocator, so remove it from every set first */
    sn_key_allocator_release(&keys, a);

    /* the index of a comes back with a higher generation */
    uint64_t c = sn_key_allocator_acquire(&keys);
    Position third = {0.0, 0.0};
    sn_sparse_set_insert(&positions, c, third);
    sn_sparse_set_contains(&positions, a); /* false, a is stale */

    /* clear is O(1) and keeps the capacity */
    sn_sparse_set_clear(&positions);

    sn_sparse_set_deinit(&positions);
    sn_key_allocator_deinit(&keys);
    return 0;
}
```

The set and the allocator are both owned by the caller. They are plain structs,
so declare them as locals or embed them in a larger one, `init` prepares them and
`deinit` releases only the arrays they manage.

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
- The sparse set compares **all 64 bits** of a key, so a key whose index was reused for
  another element is reported as absent even though it shares the index half.
- The sparse set never shrinks. `sn_sparse_set_clear` is O(1) and keeps the capacity, and
  elements past the length should be considered garbage values.
- The sparse set iterates in insertion order modified by removals, not in key order, and
  the order is not stable across removals.
- The sparse array of a sparse set is indexed by the index half of a key, so it costs
  8 bytes for every index it can address and it never shrinks. This is the dominant
  memory cost of the set, and many sets multiply it. With keys recycled by a key
  allocator the indices stay close to the peak number of live elements, so the cost is
  roughly 8 bytes times that peak, per set.
- The sparse array of a sparse set grows lazily when a key with a larger index arrives, so
  one stray large key costs memory proportional to that key: an index of 5e9 asks for a
  40GB sparse array. Keep keys dense when memory matters.
- `sn_sparse_set_insert` asserts if the key is already in the set. Update an existing
  element through `sn_sparse_set_at` instead.
- A released key is only invalid in the allocator. A set that still holds it keeps
  reporting it as present, so remove a key from every set before releasing it.
- A generation overflows back to 0 after 2^32 recycles of the same index, which needs
  roughly four billion reuses of one index and is only guarded by a debug assert.
- The allocators must not return NULL. The containers do not handle out of memory, supply
  an `SnMemoryAllocator` wrapper if the application needs to handle it.

## Dependencies

- **SnCore** — fetched automatically via FetchContent
