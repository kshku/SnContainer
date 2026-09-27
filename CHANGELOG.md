# Changelog

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
