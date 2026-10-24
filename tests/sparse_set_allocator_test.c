#include <sncontainer/sparse_set.h>
#include <sncore/types.h>
#include <snmemory/freelist.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The sparse set driven by a real SnFreeListAllocator rather than a malloc-backed
 * stand-in.
 *
 * The set keeps three arrays that grow independently, so this is also the shape
 * that makes the allocator hand a block back out from under a different array: a
 * relocation of one array has to leave the other two alone. Everything here runs
 * out of a static buffer with no malloc in reach, so a fallback to the default
 * allocator shows up as a shortfall in the buffer's free size instead of passing
 * unnoticed. */

static int failed;

#define EXPECT(cond, msg)                                           \
    do {                                                            \
        if (!(cond)) {                                              \
            fprintf(stderr, "FAIL [line %d]: %s\n", __LINE__, msg); \
            failed = 1;                                             \
        }                                                           \
    } while (0)

#define REGION_SIZE (4u * 1024u * 1024u)

/* Aligned so the allocator's first node starts exactly at the buffer. */
static union {
    unsigned char bytes[REGION_SIZE];
    uint64_t align;
} region_storage;

typedef struct {
    uint64_t id;
    double value;
    char label[16];
} Record;

#define KEY(index, generation) (((uint64_t)(generation) << 32) | (uint64_t)(index))

static int set_is_consistent(const SnSparseSet *set) {
    uint64_t length = sn_sparse_set_get_length(set);

    for (uint64_t slot = 0; slot < length; ++slot) {
        uint64_t key = sn_sparse_set_key_at(set, slot);
        if (!sn_sparse_set_contains(set, key)) return 0;
        if (sn_sparse_set_at(set, key) != (uint8_t *)set->dense + (slot * set->stride)) return 0;
    }

    for (uint64_t index = 0; index < sn_darray_get_length(set->sparse); ++index) {
        uint64_t slot = ((uint64_t *)set->sparse)[index];
        if (slot == SN_SPARSE_SET_ABSENT) continue;
        if (slot >= length) return 0;
    }

    return 1;
}

/* Every byte the set holds came out of the buffer, and deinit gives all of it
 * back. A malloc-backed allocator cannot show either half of that. */
static void test_freelist_backed_lifecycle(void) {
    SnFreeListAllocator heap;
    bool ok = sn_freelist_allocator_init(&heap, region_storage.bytes, REGION_SIZE);
    EXPECT(ok, "the freelist initialises");

    SnMemoryAllocator allocator = sn_freelist_allocator_get_allocator(&heap);
    EXPECT(allocator.alloc && allocator.realloc && allocator.free, "the vtable is complete");
    EXPECT(allocator.data == &heap, "the vtable carries the freelist as its data");

    uint64_t free_before = sn_freelist_allocator_get_free_size(&heap);

    SnSparseSet set;
    sn_sparse_set_init_with_capacity(&set, 4, Record, &allocator);

    uint64_t used = free_before - sn_freelist_allocator_get_free_size(&heap);
    EXPECT(used > 0, "the set allocated its arrays out of the buffer");

    /* Enough inserts to grow dense and ids past the initial capacity, and to grow
     * sparse as well, since the index half of the key runs well past it. */
    const uint64_t count = 500;
    for (uint64_t i = 0; i < count; ++i) {
        Record record = {.id = i, .value = (double)i * 1.5, .label = "x"};
        sn_sparse_set_insert(&set, KEY(i * 8, 1), record);
    }

    EXPECT(sn_sparse_set_get_length(&set) == count, "every insert landed");
    EXPECT(set_is_consistent(&set), "the set is consistent after growth");

    /* This is what actually exercises the relocations: each payload has to come
     * back with the value it was given, out of a block the freelist may have
     * copied it to more than once by now. */
    int all_match = 1;
    for (uint64_t i = 0; i < count; ++i) {
        Record out;
        if (!sn_sparse_set_get(&set, KEY(i * 8, 1), &out)) {
            all_match = 0;
            break;
        }
        if (out.id != i || out.value != (double)i * 1.5 || out.label[0] != 'x') {
            all_match = 0;
            break;
        }
    }
    EXPECT(all_match, "every payload survived growth intact");

    /* Write through the handle the API hands out and read it back through the
     * lookup: same backing memory, so the two have to agree. */
    Record *first = sn_sparse_set_at(&set, KEY(0, 1));
    EXPECT(first != NULL, "at returns a payload for a present key");
    if (first) {
        first->value = 99.25;
        Record check;
        bool same = sn_sparse_set_get(&set, KEY(0, 1), &check) && check.value == 99.25;
        EXPECT(same, "an at write is visible through get");
    }

    /* Remove from the middle and from the end, so the swap-with-last path runs
     * while the freelist owns the storage. */
    sn_sparse_set_remove(&set, KEY(8, 1));
    sn_sparse_set_remove(&set, KEY((count - 1) * 8, 1));
    EXPECT(sn_sparse_set_get_length(&set) == count - 2, "both removals took effect");
    EXPECT(!sn_sparse_set_contains(&set, KEY(8, 1)), "the removed key is gone");
    EXPECT(set_is_consistent(&set), "the set is consistent after removal");

    sn_sparse_set_clear(&set);
    EXPECT(sn_sparse_set_get_length(&set) == 0, "clear empties the set");

    sn_sparse_set_deinit(&set);

    uint64_t left = sn_freelist_allocator_get_free_size(&heap);
    EXPECT(left == free_before, "deinit returned every byte the set took");

    sn_freelist_allocator_deinit(&heap);
}

/* Insert and remove in a loop, so the arrays are relocated and released over and
 * over rather than growing once and staying put. */
static void test_freelist_backed_churn(void) {
    SnFreeListAllocator heap;
    bool ok = sn_freelist_allocator_init(&heap, region_storage.bytes, REGION_SIZE);
    EXPECT(ok, "the freelist initialises");

    SnMemoryAllocator allocator = sn_freelist_allocator_get_allocator(&heap);

    SnSparseSet set;
    sn_sparse_set_init(&set, int, &allocator);

    uint64_t next_key = 0;
    uint64_t live[64];
    size_t live_count = 0;

    for (int round = 0; round < 200; ++round) {
        for (int i = 0; i < 8; ++i) {
            sn_sparse_set_insert(&set, next_key, (int)next_key);
            live[live_count++] = next_key;
            ++next_key;
        }

        /* Drop every other key, which packs the dense array back down. */
        size_t kept = 0;
        for (size_t i = 0; i < live_count; ++i) {
            if (i % 2 == 0) {
                live[kept++] = live[i];
            } else {
                sn_sparse_set_remove(&set, live[i]);
            }
        }
        live_count = kept;

        EXPECT(sn_sparse_set_get_length(&set) == live_count, "length tracks the live keys");
        if (failed) break;
    }

    int all_match = 1;
    for (size_t i = 0; i < live_count; ++i) {
        if (*(int *)sn_sparse_set_at(&set, live[i]) != (int)live[i]) {
            all_match = 0;
            break;
        }
    }
    EXPECT(all_match, "every surviving payload survived the churn");
    EXPECT(set_is_consistent(&set), "the set is consistent after the churn");

    sn_sparse_set_deinit(&set);
    sn_freelist_allocator_deinit(&heap);
}

/* The freelist is a byte allocator and does not align on anyone's behalf, so this
 * checks what the set actually gets rather than assuming the set asked correctly. */
static void test_freelist_honours_element_alignment(void) {
    SnFreeListAllocator heap;
    bool ok = sn_freelist_allocator_init(&heap, region_storage.bytes, REGION_SIZE);
    EXPECT(ok, "the freelist initialises");

    SnMemoryAllocator allocator = sn_freelist_allocator_get_allocator(&heap);

    typedef struct {
        double a;
        double b;
        double c;
    } Wide;

    SnSparseSet set;
    sn_sparse_set_init_with_capacity(&set, 4, Wide, &allocator);

    for (uint64_t i = 0; i < 64; ++i) {
        Wide w = {.a = (double)i, .b = (double)i, .c = (double)i};
        sn_sparse_set_insert(&set, KEY(i, 1), w);
    }

    Wide *dense = (Wide *)set.dense;
    int aligned = 1;
    for (uint64_t slot = 0; slot < sn_sparse_set_get_length(&set); ++slot) {
        if ((uintptr_t)&dense[slot] % alignof(double) != 0) aligned = 0;
    }
    EXPECT(aligned, "every dense slot is aligned for its element type");

    sn_sparse_set_deinit(&set);
    sn_freelist_allocator_deinit(&heap);
}

/* The exact shape that was reported as a crash: a small initial capacity, one key
 * well below the rest, and then a run of keys that walks the dense and ids arrays
 * through several relocations while the sparse array stays put. The keys are picked
 * so that one of those relocations lands on an adjacent free node that is big enough
 * to pass the allocator's size check but too small once its alignment reserve is
 * counted, which used to carve a free node past the end of the block and corrupt the
 * bookkeeping of whichever array sat above it.
 *
 * The key values are load-bearing, so they are spelled out rather than generated. */
static void test_reported_relocation_failure(void) {
    typedef struct {
        float x, y;
    } Point;

    static union {
        unsigned char bytes[REGION_SIZE];
        uint64_t align;
    } region;

    SnFreeListAllocator heap;
    bool ok = sn_freelist_allocator_init(&heap, region.bytes, REGION_SIZE);
    EXPECT(ok, "the freelist initialises over its own buffer");

    SnMemoryAllocator allocator = sn_freelist_allocator_get_allocator(&heap);

    SnSparseSet set;
    sn_sparse_set_init_with_capacity(&set, 5, Point, &allocator);

    Point origin = {1.5f, -2.25f};
    sn_sparse_set_insert(&set, 7, origin);

    for (uint64_t k = 100; k < 140; ++k) {
        Point q = {(float)k, 0.0f};
        sn_sparse_set_insert(&set, k, q);
    }

    EXPECT(sn_sparse_set_get_length(&set) == 41, "all 41 keys are in the set");

    Point out;
    bool first_ok = sn_sparse_set_get(&set, 7, &out);
    EXPECT(first_ok && out.x == 1.5f && out.y == -2.25f, "the first key kept its payload");

    int all_present = 1;
    for (uint64_t k = 100; k < 140; ++k) {
        if (!sn_sparse_set_contains(&set, k)) {
            all_present = 0;
            break;
        }
        if (!sn_sparse_set_get(&set, k, &out) || out.x != (float)k) {
            all_present = 0;
            break;
        }
    }
    EXPECT(all_present, "every key from the run survived the relocations");

    sn_sparse_set_deinit(&set);
    sn_freelist_allocator_deinit(&heap);
}

int main(void) {
    test_freelist_backed_lifecycle();
    test_freelist_backed_churn();
    test_freelist_honours_element_alignment();
    test_reported_relocation_failure();
    return failed;
}
