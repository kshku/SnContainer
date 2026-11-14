# Changelog

## [0.6.0] - 2026-11-14

### Added
- `sn_darray_reserve` grows the capacity to at least a given size without
  changing the length or the elements

### Changed
- The containers handle allocation failure instead of assuming the allocator
  never returns NULL. A failed call has no observable effect: the contents and
  the headers stay unchanged and the container remains safe to retry or
  deinitialize
- `sn_darray_resize`, `sn_darray_push`, `sn_darray_push_at`,
  `sn_darray_push_elements`, `sn_darray_push_at_elements`,
  `sn_darray_push_from_ptr`, `sn_darray_push_at_from_ptr` and
  `sn_darray_shrink_to_fit` return `bool`; they still work as statements, so
  existing callers compile unchanged
- `sn_sparse_set_init`, `sn_sparse_set_init_with_capacity` and
  `sn_sparse_set_insert` return `bool`; `sn_sparse_set_insert` is now an
  expression and evaluates the set pointer once
- `sn_key_allocator_init` returns `bool` and `sn_key_allocator_acquire` returns
  0 on allocation failure; `sn_key_allocator_release` pushes to the free list
  before invalidating the key, so a failed release leaves the key valid
- `sn_sparse_set_insert` reserves the id and the element arrays before mutating
  either, so a failed element allocation cannot leave a key without an element
- Note: the shared build changes the return types of the exported `impl_*`
  functions, so it is an ABI change even though the source stays compatible

### Removed
- The requirement that the allocator must not return NULL

## [0.5.1] - 2026-11-10

### Fixed
- `sn_darray_shrink_to_fit` and `sn_darray_resize` keep a capacity of at least
  one, so shrinking an empty darray no longer makes the next push loop forever
- Pushing or inserting zero elements no longer grows the darray
- Remove a stray semicolon from the `sn_darray_shrink_to_fit` macro
- Document that push sources must not point into the same darray

## [0.5.0] - 2026-10-24

### Added
- Push multiple elements to darray
- sn_darray_shrink_to_fit to free unused memory
- sn_darray_resize maintains the darray size consistent

## [0.4.0] - 2026-10-02

### Added
- sn_darray_push_from_ptr and sn_darray_push_at_from_ptr to take pointer to the element to insert into darray
- sn_sparse_set_insert_from_ptr to take pointer to the element to insert into the sparse set 

## [0.3.5] - 2026-10-02

### Changed
- Rename the 'pointer to' parameter name in macros to have suffix 'p'

## [0.3.4] - 2026-10-02

### Changed
- Take snmemory v0.3.6 rather than v0.3.5

## [0.3.3] - 2026-10-02

### Changed
- Take sncore v0.3.2 rather than v0.3.1
- Take snmemory v0.3.5 rather than v0.3.2

## [0.3.2] - 2026-10-02

### Changed
- -Wconversion and -Wsign-conversion are on for gcc and clang. The tests
  allocated from an int loop bound, which promoted implicitly into the malloc
  and calloc size arguments

## [0.3.1] - 2026-09-28

### Fixed
- Include the header that declares sn_std_allocator in the three sources that
  default to it. The symbol moved from SnCore to SnMemory, and the sources had
  been relying on a declaration they never included, so anything built against
  SnCore past v0.3.0 failed to compile. Nothing here links SnMemory, which is
  where the symbol lives now, so it only built while the old SnCore was pinned

### Changed
- Depend on SnMemory, and take SnCore v0.3.1 rather than v0.2.0. SnCore past
  v0.3.0 no longer defines sn_std_allocator, so both are needed rather than
  either

## [0.3.0] - 2026-09-27

### Added
- Key allocator implementation, it hands out generational `uint64_t` keys and recycles the index of a released key

### Changed
- Sparse set is built on darray and is owned by the caller through `sn_sparse_set_init` and `sn_sparse_set_deinit`
- Sparse set compares all 64 bits of a key, so a key whose index was reused is reported as absent

### Fixed
- Darray no longer writes past its allocation when it is created with a capacity of 0

## [0.2.0] - 2026-09-27

### Added
- Sparse set implementation

## [0.1.0] - 2026-06-12

### Added
- Darray implementation
