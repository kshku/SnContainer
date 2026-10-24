#include <sncontainer/key_allocator.h>
#include <sncore/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failed;

#define EXPECT(cond, msg)                                           \
    do {                                                            \
        if (!(cond)) {                                              \
            fprintf(stderr, "FAIL [line %d]: %s\n", __LINE__, msg); \
            failed = 1;                                             \
        }                                                           \
    } while (0)

/* -- custom allocator for tracking --------------------------------------- */

typedef struct {
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t realloc_count;
} TestAllocData;

static void *test_alloc(void *data, uint64_t size, uint64_t align) {
    (void)align;
    ++((TestAllocData *)data)->alloc_count;
    return malloc((size_t)size);
}

static void *test_realloc(void *data, void *ptr, uint64_t new_size, uint64_t align) {
    (void)align;
    ++((TestAllocData *)data)->realloc_count;
    return realloc(ptr, (size_t)new_size);
}

static void test_free(void *data, void *ptr) {
    ++((TestAllocData *)data)->free_count;
    free(ptr);
}

#define INDEX_OF(key) ((key) & SN_KEY_ALLOCATOR_INDEX_MASK)
#define GENERATION_OF(key) ((key) >> 32)

/* -- tests --------------------------------------------------------------- */

static void test_init_deinit(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    EXPECT(sn_key_allocator_get_live_count(&keys) == 0, "no live keys initially");
    EXPECT(sn_key_allocator_get_free_count(&keys) == 0, "no free keys initially");
    EXPECT(sn_key_allocator_get_next_index(&keys) == 0, "next index starts at 0");

    sn_key_allocator_deinit(&keys);
}

static void test_acquire_fresh(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);
    uint64_t b = sn_key_allocator_acquire(&keys);
    uint64_t c = sn_key_allocator_acquire(&keys);

    EXPECT(a != 0 && b != 0 && c != 0, "no key is ever 0");
    EXPECT(GENERATION_OF(a) == SN_KEY_ALLOCATOR_FIRST_GENERATION, "first generation is 1");
    EXPECT(INDEX_OF(a) == 0, "first index is 0");
    EXPECT(INDEX_OF(b) == 1, "second index is 1");
    EXPECT(INDEX_OF(c) == 2, "third index is 2");
    EXPECT(a != b && b != c && a != c, "keys are distinct");

    EXPECT(sn_key_allocator_is_valid(&keys, a), "a is valid");
    EXPECT(sn_key_allocator_is_valid(&keys, b), "b is valid");
    EXPECT(sn_key_allocator_is_valid(&keys, c), "c is valid");

    EXPECT(sn_key_allocator_get_live_count(&keys) == 3, "three live keys");
    EXPECT(sn_key_allocator_get_next_index(&keys) == 3, "next index advanced past all three");

    sn_key_allocator_deinit(&keys);
}

static void test_release_invalidates(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);
    EXPECT(sn_key_allocator_release(&keys, a), "release of a valid key succeeds");
    EXPECT(!sn_key_allocator_is_valid(&keys, a), "released key is invalid immediately");
    EXPECT(!sn_key_allocator_release(&keys, a), "releasing twice fails");
    EXPECT(sn_key_allocator_get_live_count(&keys) == 0, "live count back to zero");
    EXPECT(sn_key_allocator_get_free_count(&keys) == 1, "one key waiting for reuse");

    sn_key_allocator_deinit(&keys);
}

static void test_release_keeps_other_keys_valid(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);
    uint64_t b = sn_key_allocator_acquire(&keys);
    sn_key_allocator_release(&keys, a);

    EXPECT(!sn_key_allocator_is_valid(&keys, a), "released key is invalid");
    EXPECT(sn_key_allocator_is_valid(&keys, b), "unrelated key stays valid");
    EXPECT(sn_key_allocator_get_live_count(&keys) == 1, "one key still live");

    sn_key_allocator_deinit(&keys);
}

static void test_generation_bump_on_reuse(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);
    sn_key_allocator_release(&keys, a);

    uint64_t b = sn_key_allocator_acquire(&keys);

    EXPECT(INDEX_OF(b) == INDEX_OF(a), "the index is reused");
    EXPECT(GENERATION_OF(b) == GENERATION_OF(a) + 1, "the generation is bumped by one");
    EXPECT(a != b, "the recycled key differs from the stale one");
    EXPECT(!sn_key_allocator_is_valid(&keys, a), "the stale key never becomes valid again");
    EXPECT(sn_key_allocator_is_valid(&keys, b), "the new key is valid");
    EXPECT(sn_key_allocator_get_next_index(&keys) == 1, "reuse consumes no fresh index");

    sn_key_allocator_deinit(&keys);
}

static void test_repeated_recycle_same_index(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t stale[4];
    stale[0] = sn_key_allocator_acquire(&keys);
    sn_key_allocator_release(&keys, stale[0]);

    for (int i = 1; i < 4; ++i) {
        stale[i] = sn_key_allocator_acquire(&keys);
        EXPECT(INDEX_OF(stale[i]) == INDEX_OF(stale[0]), "same index every round");
        EXPECT(GENERATION_OF(stale[i]) == (uint64_t)i + 1, "generation climbs by one each round");
        sn_key_allocator_release(&keys, stale[i]);
    }

    for (int i = 0; i < 4; ++i)
        EXPECT(!sn_key_allocator_is_valid(&keys, stale[i]), "every stale key stays invalid");

    uint64_t last = sn_key_allocator_acquire(&keys);
    EXPECT(INDEX_OF(last) == INDEX_OF(stale[0]), "index still the same after four rounds");
    EXPECT(sn_key_allocator_is_valid(&keys, last), "the final key is valid");

    sn_key_allocator_deinit(&keys);
}

static void test_key_zero_is_invalid(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);

    EXPECT(!sn_key_allocator_is_valid(&keys, 0), "key 0 is never valid");
    EXPECT(!sn_key_allocator_release(&keys, 0), "releasing key 0 fails");
    EXPECT(sn_key_allocator_is_valid(&keys, a), "acquiring does not disturb key 0 being invalid");
    EXPECT(sn_key_allocator_get_live_count(&keys) == 1, "live count unchanged");

    sn_key_allocator_deinit(&keys);
}

static void test_unissued_index_is_invalid(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t bogus = (SN_KEY_ALLOCATOR_FIRST_GENERATION << 32) | 500;

    EXPECT(!sn_key_allocator_is_valid(&keys, bogus), "a key for a never issued index is invalid");
    EXPECT(!sn_key_allocator_release(&keys, bogus), "releasing such a key fails");
    EXPECT(sn_key_allocator_get_free_count(&keys) == 0, "failed release queued nothing");
    EXPECT(sn_key_allocator_get_live_count(&keys) == 0, "live count untouched");

    sn_key_allocator_deinit(&keys);
}

static void test_out_of_order_reuse(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);
    uint64_t b = sn_key_allocator_acquire(&keys);
    uint64_t c = sn_key_allocator_acquire(&keys);

    sn_key_allocator_release(&keys, b);
    sn_key_allocator_release(&keys, a);

    uint64_t first = sn_key_allocator_acquire(&keys);
    uint64_t second = sn_key_allocator_acquire(&keys);

    EXPECT(INDEX_OF(first) == INDEX_OF(a), "most recently released index is reused first");
    EXPECT(INDEX_OF(second) == INDEX_OF(b), "then the earlier one");
    EXPECT(INDEX_OF(first) != INDEX_OF(second), "the two reuses land on different indices");

    EXPECT(!sn_key_allocator_is_valid(&keys, a), "a is stale");
    EXPECT(!sn_key_allocator_is_valid(&keys, b), "b is stale");
    EXPECT(sn_key_allocator_is_valid(&keys, c), "c was never released and stays valid");
    EXPECT(sn_key_allocator_is_valid(&keys, first), "first reuse is valid");
    EXPECT(sn_key_allocator_is_valid(&keys, second), "second reuse is valid");

    sn_key_allocator_deinit(&keys);
}

static void test_many_keys(void) {
    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, NULL);

    const int N = 1000;
    uint64_t *issued = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)N);
    for (int i = 0; i < N; ++i) {
        issued[i] = sn_key_allocator_acquire(&keys);
        EXPECT(sn_key_allocator_is_valid(&keys, issued[i]), "every freshly issued key is valid");
    }
    EXPECT(sn_key_allocator_get_live_count(&keys) == (uint64_t)N, "all keys live");
    EXPECT(sn_key_allocator_get_next_index(&keys) == (uint64_t)N, "all indices issued once");

    for (int i = 0; i < N; ++i)
        EXPECT(sn_key_allocator_release(&keys, issued[i]), "every key releases");
    EXPECT(sn_key_allocator_get_live_count(&keys) == 0, "no keys live after releasing all");
    EXPECT(sn_key_allocator_get_free_count(&keys) == (uint64_t)N, "all keys queued for reuse");

    for (int i = 0; i < N; ++i) {
        uint64_t reused = sn_key_allocator_acquire(&keys);
        EXPECT(INDEX_OF(reused) < (uint64_t)N, "reuse stays inside the original index space");
        EXPECT(GENERATION_OF(reused) == 2, "reused keys are at generation 2");
    }
    EXPECT(sn_key_allocator_get_next_index(&keys) == (uint64_t)N, "reuse issued no new indices");
    EXPECT(sn_key_allocator_get_live_count(&keys) == (uint64_t)N, "all keys live again");

    for (int i = 0; i < N; ++i)
        EXPECT(!sn_key_allocator_is_valid(&keys, issued[i]), "no stale key is valid");

    free(issued);
    sn_key_allocator_deinit(&keys);
}

static void test_two_allocators_are_independent(void) {
    SnKeyAllocator first, second;
    sn_key_allocator_init(&first, NULL);
    sn_key_allocator_init(&second, NULL);

    uint64_t a = sn_key_allocator_acquire(&first);
    uint64_t b = sn_key_allocator_acquire(&second);

    EXPECT(INDEX_OF(a) == 0 && INDEX_OF(b) == 0, "both allocators start their index space at 0");
    EXPECT(a == b, "two fresh allocators hand out the same first key");

    sn_key_allocator_release(&first, a);
    EXPECT(!sn_key_allocator_is_valid(&first, a), "released in the first allocator");
    EXPECT(sn_key_allocator_is_valid(&second, b), "the second allocator is unaffected");

    sn_key_allocator_deinit(&first);
    sn_key_allocator_deinit(&second);
}

static void test_custom_allocator(void) {
    TestAllocData data = {0, 0, 0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    SnKeyAllocator keys;
    sn_key_allocator_init(&keys, &alloc);
    EXPECT(data.alloc_count == 2, "the two internal arrays are allocated once each");

    for (int i = 0; i < 40; ++i) sn_key_allocator_acquire(&keys);
    EXPECT(data.realloc_count >= 1, "an internal array grew while acquiring");

    sn_key_allocator_deinit(&keys);
    EXPECT(data.free_count == 2, "deinit frees both internal arrays and nothing else");
}

/* -- main ---------------------------------------------------------------- */

int main(void) {
    test_init_deinit();
    test_acquire_fresh();
    test_release_invalidates();
    test_release_keeps_other_keys_valid();
    test_generation_bump_on_reuse();
    test_repeated_recycle_same_index();
    test_key_zero_is_invalid();
    test_unissued_index_is_invalid();
    test_out_of_order_reuse();
    test_many_keys();
    test_two_allocators_are_independent();
    test_custom_allocator();
    return failed;
}
