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

/* -- tests --------------------------------------------------------------- */

static void test_create_destroy(void) {
    int *set = sn_sparse_set_create(int, NULL);
    EXPECT(set != NULL, "create int set");
    EXPECT(sn_sparse_set_get_length(set) == 0, "initial length is 0");
    EXPECT(sn_sparse_set_get_capacity(set) == SN_SPARSE_SET_DEFAULT_CAPACITY, "initial capacity");

    uint64_t sparse_capacity = sn_sparse_set_get_sparse_capacity(set);
    EXPECT(sparse_capacity == SN_SPARSE_SET_DEFAULT_CAPACITY, "initial sparse capacity");
    sn_sparse_set_destroy(set);
}

static void test_create_with_capacity(void) {
    double *set = sn_sparse_set_create_with_capacity(100, double, NULL);
    EXPECT(set != NULL, "create with capacity");
    EXPECT(sn_sparse_set_get_capacity(set) == 100, "requested capacity");
    EXPECT(sn_sparse_set_get_length(set) == 0, "length 0 initially");
    sn_sparse_set_destroy(set);
}

static void test_insert_and_lookup(void) {
    int *set = sn_sparse_set_create(int, NULL);

    sn_sparse_set_insert(&set, 3, 30);
    sn_sparse_set_insert(&set, 7, 70);

    EXPECT(sn_sparse_set_get_length(set) == 2, "length after two inserts");
    EXPECT(sn_sparse_set_contains(set, 3), "contains inserted id 3");
    EXPECT(sn_sparse_set_contains(set, 7), "contains inserted id 7");
    EXPECT(!sn_sparse_set_contains(set, 5), "does not contain never inserted id");

    EXPECT(*sn_sparse_set_at(set, 3) == 30, "at returns value of id 3");
    EXPECT(*sn_sparse_set_at(set, 7) == 70, "at returns value of id 7");
    EXPECT(sn_sparse_set_at(set, 5) == NULL, "at returns NULL for absent id");

    int value = 0;
    EXPECT(sn_sparse_set_get(set, 3, &value), "get succeeds for present id");
    EXPECT(value == 30, "get copies value of id 3");
    EXPECT(sn_sparse_set_get(set, 7, &value), "get succeeds for present id 7");
    EXPECT(value == 70, "get copies value of id 7");
    EXPECT(!sn_sparse_set_get(set, 5, &value), "get fails for absent id");
    EXPECT(value == 70, "get leaves out param untouched on failure");

    sn_sparse_set_destroy(set);
}

static void test_dense_packing(void) {
    int *set = sn_sparse_set_create(int, NULL);

    sn_sparse_set_insert(&set, 10, 100);
    sn_sparse_set_insert(&set, 4, 40);
    sn_sparse_set_insert(&set, 25, 250);

    EXPECT(sn_sparse_set_data(set) == set, "data returns the dense payload");
    EXPECT(set[0] == 100, "dense slot 0 holds first inserted value");
    EXPECT(set[1] == 40, "dense slot 1 holds second inserted value");
    EXPECT(set[2] == 250, "dense slot 2 holds third inserted value");

    EXPECT(sn_sparse_set_id_at(set, 0) == 10, "id_at 0");
    EXPECT(sn_sparse_set_id_at(set, 1) == 4, "id_at 1");
    EXPECT(sn_sparse_set_id_at(set, 2) == 25, "id_at 2");

    sn_sparse_set_destroy(set);
}

static void test_struct_payload(void) {
    Record *set = sn_sparse_set_create(Record, NULL);

    Record r1 = {1, 3.14, "hello"};
    Record r2 = {2, 2.71, "world"};
    sn_sparse_set_insert(&set, 0, r1);
    sn_sparse_set_insert(&set, 1, r2);

    EXPECT(sn_sparse_set_get_length(set) == 2, "length after inserts");
    EXPECT(sn_sparse_set_at(set, 0)->id == 1, "struct field id");
    EXPECT(sn_sparse_set_at(set, 0)->value == 3.14, "struct field value");
    EXPECT(strcmp(sn_sparse_set_at(set, 1)->label, "world") == 0, "struct field label");

    sn_sparse_set_destroy(set);
}

static void test_update_through_at(void) {
    int *set = sn_sparse_set_create(int, NULL);

    sn_sparse_set_insert(&set, 2, 20);
    *sn_sparse_set_at(set, 2) = 99;

    int value = 0;
    EXPECT(sn_sparse_set_get(set, 2, &value), "get after update");
    EXPECT(value == 99, "update through at is visible");
    EXPECT(set[0] == 99, "update is visible in the dense array");

    sn_sparse_set_destroy(set);
}

static void test_remove_middle(void) {
    int *set = sn_sparse_set_create(int, NULL);
    for (int i = 0; i < 4; ++i) sn_sparse_set_insert(&set, i, (i + 1) * 10);

    EXPECT(sn_sparse_set_remove(set, 1), "remove middle element");
    EXPECT(sn_sparse_set_get_length(set) == 3, "length after remove");
    EXPECT(!sn_sparse_set_contains(set, 1), "removed id is gone");

    EXPECT(set[0] == 10, "slot before the hole is untouched");
    EXPECT(set[1] == 40, "last element is swapped into the hole");
    EXPECT(set[2] == 30, "element after the hole is untouched");

    EXPECT(sn_sparse_set_id_at(set, 0) == 0, "id_at 0 after remove");
    EXPECT(sn_sparse_set_id_at(set, 1) == 3, "swapped id follows the element");
    EXPECT(sn_sparse_set_id_at(set, 2) == 2, "id_at 2 after remove");

    int value = 0;
    EXPECT(sn_sparse_set_get(set, 3, &value), "get for the swapped id");
    EXPECT(value == 40, "swapped element is reachable by its id");
    EXPECT(sn_sparse_set_contains(set, 2), "id 2 still present");

    sn_sparse_set_destroy(set);
}

static void test_remove_last(void) {
    int *set = sn_sparse_set_create(int, NULL);
    for (int i = 0; i < 3; ++i) sn_sparse_set_insert(&set, i, (i + 1) * 10);

    EXPECT(sn_sparse_set_remove(set, 2), "remove the last element");
    EXPECT(sn_sparse_set_get_length(set) == 2, "length after removing the last element");
    EXPECT(!sn_sparse_set_contains(set, 2), "removed last id is gone");
    EXPECT(set[0] == 10, "first element untouched");
    EXPECT(set[1] == 20, "second element untouched");

    sn_sparse_set_destroy(set);
}

static void test_remove_only_element(void) {
    int *set = sn_sparse_set_create(int, NULL);
    sn_sparse_set_insert(&set, 42, 4200);

    EXPECT(sn_sparse_set_remove(set, 42), "remove the only element");
    EXPECT(sn_sparse_set_get_length(set) == 0, "length 0 after removing everything");
    EXPECT(!sn_sparse_set_contains(set, 42), "set is empty");

    sn_sparse_set_destroy(set);
}

static void test_remove_absent(void) {
    int *set = sn_sparse_set_create(int, NULL);
    sn_sparse_set_insert(&set, 1, 10);

    EXPECT(!sn_sparse_set_remove(set, 5), "remove of an absent id fails");
    EXPECT(!sn_sparse_set_remove(set, 100000), "remove of an id beyond the sparse array fails");
    EXPECT(sn_sparse_set_get_length(set) == 1, "length unchanged by failed removes");
    EXPECT(sn_sparse_set_contains(set, 1), "live element survives failed removes");

    sn_sparse_set_destroy(set);
}

static void test_read_beyond_sparse_capacity(void) {
    int *set = sn_sparse_set_create(int, NULL);
    sn_sparse_set_insert(&set, 0, 100);

    int value = 7;
    EXPECT(!sn_sparse_set_contains(set, 999999), "contains is false beyond the sparse array");
    EXPECT(sn_sparse_set_at(set, 999999) == NULL, "at is NULL beyond the sparse array");
    EXPECT(!sn_sparse_set_get(set, 999999, &value), "get fails beyond the sparse array");
    EXPECT(value == 7, "get leaves out param untouched beyond the sparse array");

    sn_sparse_set_destroy(set);
}

static void test_sparse_growth(void) {
    int *set = sn_sparse_set_create(int, NULL);

    uint64_t sparse_capacity = sn_sparse_set_get_sparse_capacity(set);
    EXPECT(sparse_capacity == SN_SPARSE_SET_DEFAULT_CAPACITY, "sparse capacity before growth");

    sn_sparse_set_insert(&set, 1000, 100000);
    EXPECT(sn_sparse_set_get_sparse_capacity(set) > 1000, "sparse array grew for a large id");
    EXPECT(sn_sparse_set_contains(set, 1000), "large id is present");
    EXPECT(*sn_sparse_set_at(set, 1000) == 100000, "large id maps to its element");
    EXPECT(!sn_sparse_set_contains(set, 999), "neighbouring id is still absent");

    sn_sparse_set_insert(&set, 7, 70);
    sparse_capacity = sn_sparse_set_get_sparse_capacity(set);
    EXPECT(sparse_capacity > 1000, "small id does not shrink the sparse array");
    EXPECT(*sn_sparse_set_at(set, 7) == 70, "small id inserted after growth");

    sn_sparse_set_destroy(set);
}

static void test_dense_growth(void) {
    int *set = sn_sparse_set_create(int, NULL);
    uint64_t capacity = sn_sparse_set_get_capacity(set);

    for (uint64_t i = 0; i < capacity + 1; ++i) {
        sn_sparse_set_insert(&set, (uint32_t)i, (int)(i * 2));
    }

    EXPECT(sn_sparse_set_get_length(set) == capacity + 1, "length after over-capacity insert");
    EXPECT(sn_sparse_set_get_capacity(set) > capacity, "capacity grew");
    for (uint64_t i = 0; i < capacity + 1; ++i) {
        EXPECT(*sn_sparse_set_at(set, (uint32_t)i) == (int)(i * 2),
               "values preserved after "
               "growth");
    }

    sn_sparse_set_destroy(set);
}

static void test_clear(void) {
    int *set = sn_sparse_set_create(int, NULL);
    for (uint64_t i = 0; i < 10; ++i) sn_sparse_set_insert(&set, (uint32_t)i, (int)i);

    sn_sparse_set_clear(set);

    EXPECT(sn_sparse_set_get_length(set) == 0, "length after clear");
    EXPECT(sn_sparse_set_get_capacity(set) >= SN_SPARSE_SET_DEFAULT_CAPACITY,
           "capacity preserved "
           "after clear");
    for (uint64_t i = 0; i < 10; ++i) {
        EXPECT(!sn_sparse_set_contains(set, (uint32_t)i), "all ids absent after clear");
    }

    sn_sparse_set_insert(&set, 3, 33);
    EXPECT(sn_sparse_set_get_length(set) == 1, "insert after clear");
    EXPECT(*sn_sparse_set_at(set, 3) == 33, "element inserted after clear");

    sn_sparse_set_destroy(set);
}

static void test_reserve(void) {
    TestAllocData data = {0, 0, 0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    int *set = sn_sparse_set_create_with_capacity(2, int, &alloc);
    EXPECT(data.alloc_count == 3, "create allocates the header, the sparse and the ids array");
    EXPECT(data.realloc_count == 0, "create does not reallocate");

    sn_sparse_set_reserve(&set, 100, 1000);

    EXPECT(sn_sparse_set_get_capacity(set) >= 100, "dense capacity after reserve");
    EXPECT(sn_sparse_set_get_sparse_capacity(set) > 1000, "sparse capacity after reserve");
    EXPECT(data.realloc_count == 3, "reserve reallocates the dense array, the ids array and the "
                                    "sparse array");

    for (uint64_t i = 0; i < 50; ++i) sn_sparse_set_insert(&set, (uint32_t)i * 20, (int)i);
    EXPECT(data.realloc_count == 3, "inserts after reserve do not reallocate");
    EXPECT(sn_sparse_set_get_length(set) == 50, "length after reserved inserts");

    sn_sparse_set_destroy(set);
    EXPECT(data.free_count == 3, "destroy frees all three allocations");
}

static void test_reserve_no_shrink(void) {
    int *set = sn_sparse_set_create(int, NULL);
    for (uint64_t i = 0; i < 10; ++i) sn_sparse_set_insert(&set, (uint32_t)i, (int)i);

    uint64_t capacity = sn_sparse_set_get_capacity(set);
    uint64_t sparse_capacity = sn_sparse_set_get_sparse_capacity(set);

    sn_sparse_set_reserve(&set, 1, 0);

    EXPECT(sn_sparse_set_get_capacity(set) == capacity, "reserve never shrinks the dense array");
    EXPECT(sn_sparse_set_get_sparse_capacity(set) == sparse_capacity,
           "reserve never shrinks the "
           "sparse array");

    sn_sparse_set_destroy(set);
}

static void test_custom_allocator(void) {
    TestAllocData data = {0, 0, 0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    int *set = sn_sparse_set_create(int, &alloc);
    EXPECT(set != NULL, "create with custom allocator");
    EXPECT(data.alloc_count == 3, "custom alloc called three times");

    for (uint64_t i = 0; i < 100; ++i) sn_sparse_set_insert(&set, (uint32_t)(i * 7), (int)i);
    EXPECT(data.realloc_count >= 1, "custom realloc called during growth");

    sn_sparse_set_destroy(set);
    EXPECT(data.free_count == data.alloc_count, "every allocation is freed");
}

static void test_many_elements(void) {
    const uint64_t N = 10000;
    int *set = sn_sparse_set_create(int, NULL);

    for (uint64_t i = 0; i < N; ++i) sn_sparse_set_insert(&set, (uint32_t)(i * 3), (int)(i * 7));
    EXPECT(sn_sparse_set_get_length(set) == N, "length after many inserts");

    for (uint64_t i = 0; i < N; i += 2)
        EXPECT(sn_sparse_set_remove(set, (uint32_t)(i * 3)), "remove");
    EXPECT(sn_sparse_set_get_length(set) == N / 2, "length after removals");

    for (uint64_t i = 0; i < N; ++i) {
        int value = -1;
        bool present = sn_sparse_set_get(set, (uint32_t)(i * 3), &value);
        if (i % 2 == 0) {
            EXPECT(!present, "removed id is gone");
        } else {
            EXPECT(present, "surviving id is present");
            EXPECT(value == (int)(i * 7), "surviving value is intact");
        }
    }

    /* every live element must be reachable through its own id exactly once */
    for (uint64_t i = 0; i < sn_sparse_set_get_length(set); ++i) {
        uint32_t id = sn_sparse_set_id_at(set, i);
        int value = -1;
        EXPECT(sn_sparse_set_get(set, id, &value), "dense slot is reachable by its id");
        EXPECT(value == set[i], "dense slot and id agree");
    }

    sn_sparse_set_destroy(set);
}

/* -- main ---------------------------------------------------------------- */

int main(void) {
    test_create_destroy();
    test_create_with_capacity();
    test_insert_and_lookup();
    test_dense_packing();
    test_struct_payload();
    test_update_through_at();
    test_remove_middle();
    test_remove_last();
    test_remove_only_element();
    test_remove_absent();
    test_read_beyond_sparse_capacity();
    test_sparse_growth();
    test_dense_growth();
    test_clear();
    test_reserve();
    test_reserve_no_shrink();
    test_custom_allocator();
    test_many_elements();
    return failed;
}
