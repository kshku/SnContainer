# Changelog

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
