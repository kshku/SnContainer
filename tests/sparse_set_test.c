#include <sncontainer/key_allocator.h>
#include <sncontainer/sparse_set.h>
#include <sncore/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* -- struct for type-variation tests ------------------------------------- */

typedef struct {
    uint64_t id;
    double value;
    char label[16];
} Record;

/* -- helpers ------------------------------------------------------------- */

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

/* -- tests --------------------------------------------------------------- */

static void test_init_deinit(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    EXPECT(sn_sparse_set_get_length(&set) == 0, "initial length is 0");
    EXPECT(sn_sparse_set_get_capacity(&set) == SN_SPARSE_SET_DEFAULT_CAPACITY, "dense capacity");
    uint64_t sparse_capacity = sn_sparse_set_get_sparse_capacity(&set);
    EXPECT(sparse_capacity == SN_SPARSE_SET_DEFAULT_CAPACITY, "sparse capacity");
    EXPECT(!sn_sparse_set_contains(&set, 0), "empty set contains nothing");

    sn_sparse_set_deinit(&set);
}

static void test_init_with_capacity(void) {
    SnSparseSet set;
    sn_sparse_set_init_with_capacity(&set, 100, double, NULL);

    EXPECT(sn_sparse_set_get_length(&set) == 0, "length 0 initially");
    EXPECT(sn_sparse_set_get_capacity(&set) == 100, "requested dense capacity");
    EXPECT(sn_sparse_set_get_sparse_capacity(&set) == 100, "requested sparse capacity");

    sn_sparse_set_deinit(&set);
}

static void test_insert_and_lookup(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    sn_sparse_set_insert(&set, 3, 30);
    sn_sparse_set_insert(&set, 7, 70);

    EXPECT(sn_sparse_set_get_length(&set) == 2, "length after two inserts");
    EXPECT(sn_sparse_set_contains(&set, 3), "contains inserted key 3");
    EXPECT(sn_sparse_set_contains(&set, 7), "contains inserted key 7");
    EXPECT(!sn_sparse_set_contains(&set, 5), "does not contain never inserted key");

    EXPECT(*(int *)sn_sparse_set_at(&set, 3) == 30, "at returns the value of key 3");
    EXPECT(*(int *)sn_sparse_set_at(&set, 7) == 70, "at returns the value of key 7");
    EXPECT(sn_sparse_set_at(&set, 5) == NULL, "at returns NULL for an absent key");

    int value = 0;
    EXPECT(sn_sparse_set_get(&set, 3, &value), "get succeeds for a present key");
    EXPECT(value == 30, "get copies the value of key 3");
    EXPECT(!sn_sparse_set_get(&set, 5, &value), "get fails for an absent key");
    EXPECT(value == 30, "failed get leaves the output untouched");

    sn_sparse_set_deinit(&set);
}

static void test_dense_packing(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    const int N = 20;
    for (int i = 0; i < N; ++i) sn_sparse_set_insert(&set, (uint64_t)i, i * 10);

    EXPECT(sn_sparse_set_get_length(&set) == (uint64_t)N, "length after inserting all keys");
    for (int i = 0; i < N; ++i) {
        EXPECT(sn_sparse_set_contains(&set, (uint64_t)i), "every key is reachable");
        EXPECT(*(int *)sn_sparse_set_at(&set, (uint64_t)i) == i * 10, "every value is reachable");
    }
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree");

    sn_sparse_set_deinit(&set);
}

static void test_struct_payload(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, Record, NULL);

    Record one = {1, 3.14, "hello"};
    Record two = {2, 2.71, "world"};
    sn_sparse_set_insert(&set, 11, one);
    sn_sparse_set_insert(&set, 22, two);

    Record *got = (Record *)sn_sparse_set_at(&set, 22);
    EXPECT(got != NULL, "struct payload is reachable");
    EXPECT(got->id == 2, "struct field id");
    EXPECT(got->value == 2.71, "struct field value");
    EXPECT(strcmp(got->label, "world") == 0, "struct field label");

    sn_sparse_set_deinit(&set);
}

static void test_update_through_at(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    sn_sparse_set_insert(&set, 1, 10);
    sn_sparse_set_insert(&set, 2, 20);

    *(int *)sn_sparse_set_at(&set, 1) = 99;
    EXPECT(*(int *)sn_sparse_set_at(&set, 1) == 99, "element updated in place through at");
    EXPECT(*(int *)sn_sparse_set_at(&set, 2) == 20, "the other element is untouched");
    EXPECT(sn_sparse_set_get_length(&set) == 2, "updating does not change the length");

    sn_sparse_set_deinit(&set);
}

static void test_remove_middle(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    for (int i = 1; i <= 5; ++i) sn_sparse_set_insert(&set, (uint64_t)i, i * 10);

    EXPECT(sn_sparse_set_remove(&set, 2), "remove reports success");
    EXPECT(!sn_sparse_set_contains(&set, 2), "removed key is gone");
    EXPECT(sn_sparse_set_get_length(&set) == 4, "length dropped by one");
    EXPECT(sn_sparse_set_at(&set, 2) == NULL, "at returns NULL after removal");

    int removed = 7;
    EXPECT(!sn_sparse_set_get(&set, 2, &removed), "get fails after removal");
    EXPECT(removed == 7, "a failed get leaves the output untouched");

    for (int i = 1; i <= 5; ++i) {
        if (i == 2) continue;
        EXPECT(sn_sparse_set_contains(&set, (uint64_t)i), "the other keys survive the swap remove");
        EXPECT(*(int *)sn_sparse_set_at(&set, (uint64_t)i) == i * 10, "the other values survive");
    }
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree after a swap remove");

    sn_sparse_set_deinit(&set);
}

static void test_remove_last(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    sn_sparse_set_insert(&set, 1, 10);
    sn_sparse_set_insert(&set, 2, 20);
    sn_sparse_set_insert(&set, 3, 30);

    EXPECT(sn_sparse_set_remove(&set, 3), "removing the last slot needs no swap");
    EXPECT(sn_sparse_set_get_length(&set) == 2, "length dropped by one");
    EXPECT(!sn_sparse_set_contains(&set, 3), "the removed key is gone");
    EXPECT(*(int *)sn_sparse_set_at(&set, 1) == 10, "first value untouched");
    EXPECT(*(int *)sn_sparse_set_at(&set, 2) == 20, "second value untouched");

    sn_sparse_set_deinit(&set);
}

static void test_remove_only_element(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    sn_sparse_set_insert(&set, 9, 90);
    EXPECT(sn_sparse_set_remove(&set, 9), "removing the only element succeeds");
    EXPECT(sn_sparse_set_get_length(&set) == 0, "the set is empty");
    EXPECT(!sn_sparse_set_contains(&set, 9), "the removed key is gone");

    sn_sparse_set_deinit(&set);
}

static void test_remove_absent(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    sn_sparse_set_insert(&set, 1, 10);
    EXPECT(!sn_sparse_set_remove(&set, 2), "removing an absent key fails");
    EXPECT(!sn_sparse_set_remove(&set, 999), "removing an index past the sparse array fails");
    EXPECT(sn_sparse_set_get_length(&set) == 1, "a failed remove changes nothing");

    sn_sparse_set_remove(&set, 1);
    EXPECT(!sn_sparse_set_remove(&set, 1), "removing the same key twice fails");

    sn_sparse_set_deinit(&set);
}

static void test_reinsert_after_remove(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    sn_sparse_set_insert(&set, 4, 40);
    sn_sparse_set_remove(&set, 4);
    sn_sparse_set_insert(&set, 4, 44);

    EXPECT(sn_sparse_set_get_length(&set) == 1, "the key is back");
    EXPECT(sn_sparse_set_contains(&set, 4), "the key is reachable again");
    EXPECT(*(int *)sn_sparse_set_at(&set, 4) == 44, "the new value is the one stored");
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree");

    sn_sparse_set_deinit(&set);
}

static void test_sparse_growth(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    uint64_t big = KEY(1000, 1);
    sn_sparse_set_insert(&set, big, 7);

    EXPECT(sn_sparse_set_get_sparse_capacity(&set) == 1001, "sparse array reached the index");
    EXPECT(sn_sparse_set_get_length(&set) == 1, "growing the sparse array did not invent elements");
    EXPECT(sn_sparse_set_contains(&set, big), "the element with the large index is reachable");
    EXPECT(*(int *)sn_sparse_set_at(&set, big) == 7, "its value is intact");

    EXPECT(!sn_sparse_set_contains(&set, KEY(500, 1)), "a covered but absent index reports absent");
    EXPECT(!sn_sparse_set_contains(&set, KEY(5000, 1)), "an index past the sparse array");
    EXPECT(!sn_sparse_set_contains(&set, KEY(1000, 2)), "same index, other generation");

    sn_sparse_set_insert(&set, KEY(3, 1), 3);
    EXPECT(sn_sparse_set_get_sparse_capacity(&set) == 1001, "a smaller key does not shrink");
    EXPECT(sn_sparse_set_contains(&set, KEY(3, 1)), "the smaller key is reachable");
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree after a growth");

    sn_sparse_set_deinit(&set);
}

static void test_dense_growth(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    const int N = 50;
    for (int i = 0; i < N; ++i) sn_sparse_set_insert(&set, (uint64_t)i, i);

    EXPECT(sn_sparse_set_get_length(&set) == (uint64_t)N, "the dense array grew as needed");
    EXPECT(sn_sparse_set_get_capacity(&set) >= (uint64_t)N, "dense capacity covers them all");
    EXPECT(set_is_consistent(&set), "sparse and dense agree after growth");
    for (int i = 0; i < N; ++i) {
        EXPECT(*(int *)sn_sparse_set_at(&set, (uint64_t)i) == i, "values survive the dense growth");
    }

    sn_sparse_set_deinit(&set);
}

static void test_clear(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    for (int i = 1; i <= 3; ++i) sn_sparse_set_insert(&set, (uint64_t)i, i * 10);
    uint64_t sparse_capacity = sn_sparse_set_get_sparse_capacity(&set);
    uint64_t capacity = sn_sparse_set_get_capacity(&set);

    sn_sparse_set_clear(&set);

    EXPECT(sn_sparse_set_get_length(&set) == 0, "the set is empty after a clear");
    for (int i = 1; i <= 3; ++i)
        EXPECT(!sn_sparse_set_contains(&set, (uint64_t)i), "every key is gone after a clear");
    EXPECT(sn_sparse_set_get_sparse_capacity(&set) == sparse_capacity,
           "sparse array capacity is "
           "kept");
    EXPECT(sn_sparse_set_get_capacity(&set) == capacity, "the dense capacity is left alone");

    sn_sparse_set_deinit(&set);
}

static void test_insert_after_clear(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    for (int i = 1; i <= 3; ++i) sn_sparse_set_insert(&set, (uint64_t)i, i * 10);
    sn_sparse_set_clear(&set);

    for (int i = 1; i <= 3; ++i) {
        sn_sparse_set_insert(&set, (uint64_t)i, i * 100);
        EXPECT(sn_sparse_set_contains(&set, (uint64_t)i), "the key is reachable again");
        EXPECT(*(int *)sn_sparse_set_at(&set, (uint64_t)i) == i * 100, "it holds the new value");
    }
    EXPECT(sn_sparse_set_get_length(&set) == 3, "the set is full again");
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree after a clear and refills");

    sn_sparse_set_deinit(&set);
}

static void test_iteration_with_key_at(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    const int N = 6;
    for (int i = 0; i < N; ++i) sn_sparse_set_insert(&set, (uint64_t)(i + 1), (i + 1) * 10);
    sn_sparse_set_remove(&set, 3);

    int seen = 0;
    for (uint64_t slot = 0; slot < sn_sparse_set_get_length(&set); ++slot) {
        uint64_t key = sn_sparse_set_key_at(&set, slot);
        int *value = (int *)sn_sparse_set_at(&set, key);
        EXPECT(value != NULL, "every slot holds a key the set can resolve");
        EXPECT(*value / 10 == (int)key, "the value of a slot matches its key");
        EXPECT(key != 3, "the removed key is not visited");
        ++seen;
    }
    EXPECT(seen == N - 1, "iteration visits every live element exactly once");

    sn_sparse_set_deinit(&set);
}

static void test_standalone_keys(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    uint64_t keys[] = {0, KEY(7, 0), KEY(1, 0xFFFFFFFF), KEY(9, 0xFFFFFFFF), UINT64_C(0xABCD) << 32 | 11};
    for (uint64_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        sn_sparse_set_insert(&set, keys[i], (int)i);
    }

    EXPECT(sn_sparse_set_get_length(&set) == sizeof(keys) / sizeof(keys[0]),
           "arbitrary keys are "
           "accepted");
    for (uint64_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        EXPECT(sn_sparse_set_contains(&set, keys[i]), "an arbitrary key is reachable");
        EXPECT(*(int *)sn_sparse_set_at(&set, keys[i]) == (int)i, "an arbitrary key holds value");
    }
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree for arbitrary keys");

    sn_sparse_set_deinit(&set);
}

static void test_generation_is_part_of_the_key(void) {
    SnKeyAllocator keys;
    SnSparseSet set;
    sn_key_allocator_init(&keys, NULL);
    sn_sparse_set_init(&set, int, NULL);

    uint64_t a = sn_key_allocator_acquire(&keys);
    sn_sparse_set_insert(&set, a, 1);
    sn_sparse_set_remove(&set, a);
    EXPECT(sn_key_allocator_release(&keys, a), "release after removing from the set");

    uint64_t b = sn_key_allocator_acquire(&keys);
    EXPECT(b != a, "the recycled key differs from the stale one");
    EXPECT((a & SN_KEY_ALLOCATOR_INDEX_MASK) == (b & SN_KEY_ALLOCATOR_INDEX_MASK),
           "the index is "
           "the same");
    EXPECT((b >> 32) == (a >> 32) + 1, "the generation is higher");
    EXPECT(!sn_sparse_set_contains(&set, a), "the stale key is not in the set");
    EXPECT(!sn_sparse_set_contains(&set, b), "the new key is not in the set yet");

    sn_sparse_set_insert(&set, b, 2);
    EXPECT(sn_sparse_set_contains(&set, b), "the new key is in the set");
    EXPECT(!sn_sparse_set_contains(&set, a), "the stale key stays out");
    EXPECT(*(int *)sn_sparse_set_at(&set, b) == 2, "the new key holds the new value");
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree after a key was recycled");

    sn_sparse_set_deinit(&set);
    sn_key_allocator_deinit(&keys);
}

static void test_shared_key_across_sets(void) {
    SnKeyAllocator keys;
    SnSparseSet positions, velocities;
    sn_key_allocator_init(&keys, NULL);
    sn_sparse_set_init(&positions, int, NULL);
    sn_sparse_set_init(&velocities, int, NULL);

    for (int i = 0; i < 5; ++i) {
        uint64_t key = sn_key_allocator_acquire(&keys);
        sn_sparse_set_insert(&positions, key, 100 + i);
        sn_sparse_set_insert(&velocities, key, 200 + i);
    }

    EXPECT(sn_sparse_set_get_length(&positions) == 5, "every key is in the first set");
    EXPECT(sn_sparse_set_get_length(&velocities) == 5, "every key is in the second set");

    for (uint64_t slot = 0; slot < sn_sparse_set_get_length(&positions); ++slot) {
        uint64_t key = sn_sparse_set_key_at(&positions, slot);
        int position = *(int *)sn_sparse_set_at(&positions, key);
        int velocity = *(int *)sn_sparse_set_at(&velocities, key);
        EXPECT(velocity == position + 100, "one key addresses the same element in both sets");
    }

    uint64_t key = sn_sparse_set_key_at(&positions, 0);
    sn_sparse_set_remove(&positions, key);
    EXPECT(!sn_sparse_set_contains(&positions, key), "the key left the first set");
    EXPECT(sn_sparse_set_contains(&velocities, key), "the key is still in the second set");

    sn_sparse_set_deinit(&positions);
    sn_sparse_set_deinit(&velocities);
    sn_key_allocator_deinit(&keys);
}

static void test_lifecycle_through_allocator(void) {
    SnKeyAllocator keys;
    SnSparseSet set;
    sn_key_allocator_init(&keys, NULL);
    sn_sparse_set_init(&set, int, NULL);

    const int N = 1000;
    uint64_t *live = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)N);
    uint8_t *still_live = (uint8_t *)calloc((size_t)N, sizeof(uint8_t));
    uint64_t live_count = 0;

    for (int i = 0; i < N; ++i) {
        live[live_count] = sn_key_allocator_acquire(&keys);
        sn_sparse_set_insert(&set, live[live_count], i);
        still_live[live_count] = 1;
        ++live_count;
    }

    for (uint64_t i = 0; i < live_count; ++i) {
        EXPECT(sn_sparse_set_contains(&set, live[i]), "every issued key is in the set");
        EXPECT(sn_key_allocator_is_valid(&keys, live[i]), "every issued key is still valid");
    }

    for (uint64_t i = 0; i < live_count; i += 2) {
        EXPECT(sn_sparse_set_remove(&set, live[i]), "removing a live key succeeds");
        EXPECT(sn_key_allocator_release(&keys, live[i]), "releasing a live key succeeds");
        still_live[i] = 0;
    }

    EXPECT(sn_sparse_set_get_length(&set) == (uint64_t)(N / 2), "half the elements remain");
    EXPECT(sn_key_allocator_get_live_count(&keys) == (uint64_t)(N - N / 2), "half the keys remain");
    for (uint64_t i = 0; i < live_count; ++i) {
        if (still_live[i]) {
            EXPECT(sn_sparse_set_contains(&set, live[i]), "an unreleased key is still in the set");
        } else {
            EXPECT(!sn_sparse_set_contains(&set, live[i]), "a released key is not in the set");
        }
    }
    EXPECT(set_is_consistent(&set), "the sparse and dense arrays agree after a bulk release");
    EXPECT(sn_sparse_set_get_sparse_capacity(&set) <= (uint64_t)N * 2,
           "sparse array stays near "
           "the peak");

    free(live);
    free(still_live);
    sn_sparse_set_deinit(&set);
    sn_key_allocator_deinit(&keys);
}

static void test_custom_allocator(void) {
    TestAllocData data = {0, 0, 0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    SnSparseSet set;
    sn_sparse_set_init_with_capacity(&set, 3, int, &alloc);
    EXPECT(data.alloc_count == 3, "the three internal arrays are allocated once each");

    for (int i = 0; i < 30; ++i) sn_sparse_set_insert(&set, (uint64_t)i, i);
    EXPECT(data.realloc_count >= 1, "an internal array grew while inserting");

    sn_sparse_set_deinit(&set);
    EXPECT(data.free_count == 3, "deinit frees the three internal arrays and nothing else");
}

static void test_sparse_set_insert_from_ptr(void) {
    SnSparseSet set;
    sn_sparse_set_init(&set, int, NULL);

    int value = 30;
    sn_sparse_set_insert_from_ptr(&set, 3, &value);
    value = 70;
    sn_sparse_set_insert_from_ptr(&set, 7, &value);

    EXPECT(sn_sparse_set_get_length(&set) == 2, "length after two inserts");
    EXPECT(sn_sparse_set_contains(&set, 3), "contains inserted key 3");
    EXPECT(sn_sparse_set_contains(&set, 7), "contains inserted key 7");
    EXPECT(!sn_sparse_set_contains(&set, 5), "does not contain never inserted key");

    EXPECT(*(int *)sn_sparse_set_at(&set, 3) == 30, "at returns the value of key 3");
    EXPECT(*(int *)sn_sparse_set_at(&set, 7) == 70, "at returns the value of key 7");
    EXPECT(sn_sparse_set_at(&set, 5) == NULL, "at returns NULL for an absent key");

    value = 0;
    EXPECT(sn_sparse_set_get(&set, 3, &value), "get succeeds for a present key");
    EXPECT(value == 30, "get copies the value of key 3");
    EXPECT(!sn_sparse_set_get(&set, 5, &value), "get fails for an absent key");
    EXPECT(value == 30, "failed get leaves the output untouched");

    sn_sparse_set_deinit(&set);
}

/* -- main ---------------------------------------------------------------- */

int main(void) {
    test_init_deinit();
    test_init_with_capacity();
    test_insert_and_lookup();
    test_dense_packing();
    test_struct_payload();
    test_update_through_at();
    test_remove_middle();
    test_remove_last();
    test_remove_only_element();
    test_remove_absent();
    test_reinsert_after_remove();
    test_sparse_growth();
    test_dense_growth();
    test_clear();
    test_insert_after_clear();
    test_iteration_with_key_at();
    test_standalone_keys();
    test_generation_is_part_of_the_key();
    test_shared_key_across_sets();
    test_lifecycle_through_allocator();
    test_custom_allocator();
    test_sparse_set_insert_from_ptr();
    return failed;
}
