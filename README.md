# SnContainer

General-purpose container library written in C. Provides efficient, type-safe
data structures backed by user-supplied or default allocators.

## Containers

| Container | Description |
|-----------|-------------|
| DArray | Dynamic array with type-safe push/pop/push_at/pop_at, automatic resizing, and alignment support |

## Usage

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

## Adding to your project

```cmake
include(FetchContent)
FetchContent_Declare(sncontainer
    GIT_REPOSITORY https://github.com/kshku/SnContainer.git
    GIT_TAG <tag>  # e.g., v0.1.0
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

## Dependencies

- **SnCore** — fetched automatically via FetchContent
