#include <sncontainer/darray.h>
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
    uint64_t alloc_calls;
    uint64_t realloc_calls;
    uint64_t fail_alloc_call;
    uint64_t fail_realloc_call;
} TestAllocData;

static void *test_alloc(void *data, uint64_t size, uint64_t align) {
    (void)align;
    TestAllocData *test = (TestAllocData *)data;
    ++test->alloc_calls;
    if (test->fail_alloc_call != 0 && test->alloc_calls == test->fail_alloc_call) return NULL;
    ++test->alloc_count;
    return malloc((size_t)size);
}

static void *test_realloc(void *data, void *ptr, uint64_t new_size, uint64_t align) {
    (void)align;
    TestAllocData *test = (TestAllocData *)data;
    ++test->realloc_calls;
    if (test->fail_realloc_call != 0 && test->realloc_calls == test->fail_realloc_call) return NULL;
    ++test->realloc_count;
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
    int *arr = sn_darray_create(int, NULL);
    EXPECT(arr != NULL, "create int array");
    EXPECT(sn_darray_get_length(arr) == 0, "initial length is 0");
    EXPECT(sn_darray_get_capacity(arr) == SN_DARRAY_DEFAULT_CAPACITY, "initial capacity");
    sn_darray_destroy(arr);
}

static void test_create_with_capacity(void) {
    double *arr = sn_darray_create_with_capacity(100, double, NULL);
    EXPECT(arr != NULL, "create with capacity");
    EXPECT(sn_darray_get_capacity(arr) == 100, "requested capacity");
    EXPECT(sn_darray_get_length(arr) == 0, "length 0 initially");
    sn_darray_destroy(arr);
}

static void test_push_and_length(void) {
    int *arr = sn_darray_create(int, NULL);
    for (int i = 0; i < 4; ++i) {
        sn_darray_push(&arr, i);
    }
    EXPECT(sn_darray_get_length(arr) == 4, "length after 4 pushes");
    EXPECT(sn_darray_get_capacity(arr) == SN_DARRAY_DEFAULT_CAPACITY, "capacity unchanged");
    for (int i = 0; i < 4; ++i) {
        EXPECT(arr[i] == i, "element value after push");
    }
    sn_darray_destroy(arr);
}

static void test_resize_on_push(void) {
    int *arr = sn_darray_create(int, NULL);
    uint64_t cap = sn_darray_get_capacity(arr);
    for (int i = 0; i < (int)cap + 1; ++i) {
        sn_darray_push(&arr, i);
    }
    EXPECT(sn_darray_get_length(arr) == cap + 1, "length after over-capacity push");
    EXPECT(sn_darray_get_capacity(arr) > cap, "capacity grew after resize");
    for (uint64_t i = 0; i < cap + 1; ++i) {
        EXPECT(arr[i] == (int)i, "values preserved after resize");
    }
    sn_darray_destroy(arr);
}

static void test_pop_value(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 10);
    sn_darray_push(&arr, 20);
    sn_darray_push(&arr, 30);

    int val;
    sn_darray_pop(&arr, &val);
    EXPECT(val == 30, "pop returns last value");
    EXPECT(sn_darray_get_length(arr) == 2, "length decreased after pop");

    sn_darray_pop(&arr, &val);
    EXPECT(val == 20, "pop returns second value");
    EXPECT(sn_darray_get_length(arr) == 1, "length 1 after 2 pops");
    sn_darray_destroy(arr);
}

static void test_pop_null(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 42);
    sn_darray_pop(&arr, NULL);
    EXPECT(sn_darray_get_length(arr) == 0, "pop with NULL element discards value");
    sn_darray_destroy(arr);
}

static void test_pop_empty(void) {
    int *arr = sn_darray_create(int, NULL);
    int val = -1;
    sn_darray_pop(&arr, &val);
    EXPECT(val == -1, "pop from empty array does not modify output");
    EXPECT(sn_darray_get_length(arr) == 0, "length stays 0");
    sn_darray_destroy(arr);
}

static void test_push_at_middle(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 0);
    sn_darray_push(&arr, 2);
    sn_darray_push(&arr, 3);

    int one = 1;
    sn_darray_push_at(&arr, 1, one);

    EXPECT(sn_darray_get_length(arr) == 4, "length after push_at middle");
    EXPECT(arr[0] == 0, "push_at[0]");
    EXPECT(arr[1] == 1, "push_at[1] inserted");
    EXPECT(arr[2] == 2, "push_at[2] shifted");
    EXPECT(arr[3] == 3, "push_at[3] shifted");
    sn_darray_destroy(arr);
}

static void test_push_at_end(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 0);
    sn_darray_push(&arr, 1);

    int two = 2;
    sn_darray_push_at(&arr, 2, two);

    EXPECT(sn_darray_get_length(arr) == 3, "length after push_at end");
    EXPECT(arr[2] == 2, "push_at end equals last element");
    sn_darray_destroy(arr);
}

static void test_push_at_beyond_end(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 0);

    int five = 5;
    sn_darray_push_at(&arr, 5, five);

    EXPECT(sn_darray_get_length(arr) == 6, "length after gap-filling push_at");
    EXPECT(arr[0] == 0, "original element preserved");
    EXPECT(arr[1] == 0, "gap element zeroed");
    EXPECT(arr[5] == 5, "inserted element at index 5");
    sn_darray_destroy(arr);
}

static void test_pop_at(void) {
    int *arr = sn_darray_create(int, NULL);
    for (int i = 0; i < 5; ++i) sn_darray_push(&arr, i);

    int val;
    sn_darray_pop_at(&arr, 2, &val);
    EXPECT(val == 2, "pop_at returns correct value");
    EXPECT(sn_darray_get_length(arr) == 4, "length after pop_at middle");
    EXPECT(arr[0] == 0, "pop_at[0] unchanged");
    EXPECT(arr[1] == 1, "pop_at[1] unchanged");
    EXPECT(arr[2] == 3, "pop_at[2] shifted down");
    EXPECT(arr[3] == 4, "pop_at[3] shifted down");
    sn_darray_destroy(arr);
}

static void test_pop_at_first(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 10);
    sn_darray_push(&arr, 20);

    int val;
    sn_darray_pop_at(&arr, 0, &val);
    EXPECT(val == 10, "pop_at first element");
    EXPECT(arr[0] == 20, "remaining element shifts to front");
    EXPECT(sn_darray_get_length(arr) == 1, "length 1 after pop first");
    sn_darray_destroy(arr);
}

static void test_pop_at_null(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 99);
    sn_darray_pop_at(&arr, 0, NULL);
    EXPECT(sn_darray_get_length(arr) == 0, "pop_at with NULL discards element");
    sn_darray_destroy(arr);
}

static void test_clear(void) {
    int *arr = sn_darray_create(int, NULL);
    for (int i = 0; i < 10; ++i) sn_darray_push(&arr, i);
    EXPECT(sn_darray_get_length(arr) == 10, "length before clear");
    sn_darray_clear(&arr);
    EXPECT(sn_darray_get_length(arr) == 0, "length after clear");
    EXPECT(sn_darray_get_capacity(arr) >= 5, "capacity preserved after clear");
    sn_darray_destroy(arr);
}

static void test_custom_allocator(void) {
    TestAllocData data = {0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    int *arr = sn_darray_create_with_capacity(3, int, &alloc);
    EXPECT(arr != NULL, "create with custom allocator");
    EXPECT(data.alloc_count == 1, "custom alloc called");

    for (int i = 0; i < 10; ++i) sn_darray_push(&arr, i);
    EXPECT(data.realloc_count >= 1, "custom realloc called during resize");

    sn_darray_destroy(arr);
    EXPECT(data.free_count == 1, "custom free called on destroy");
}

static void test_different_types(void) {
    Record *arr = sn_darray_create(Record, NULL);
    EXPECT(arr != NULL, "create struct array");

    Record r1 = {1, 3.14, "hello"};
    sn_darray_push(&arr, r1);
    EXPECT(arr[0].id == 1, "struct field id");
    EXPECT(arr[0].value == 3.14, "struct field value");
    EXPECT(strcmp(arr[0].label, "hello") == 0, "struct field label");

    sn_darray_destroy(arr);
}

static void test_large_count(void) {
    int *arr = sn_darray_create(int, NULL);
    const int N = 10000;
    for (int i = 0; i < N; ++i) sn_darray_push(&arr, i);
    EXPECT(sn_darray_get_length(arr) == (uint64_t)N, "length after 10000 pushes");
    for (int i = 0; i < N; ++i) {
        EXPECT(arr[i] == i, "values correct after many pushes");
    }
    sn_darray_destroy(arr);
}

static void test_push_from_ptr(void) {
    int *arr = sn_darray_create(int, NULL);
    for (int i = 0; i < 10; ++i) sn_darray_push_from_ptr(&arr, &i);
    for (int i = 0; i < 4; ++i) sn_darray_push_at_from_ptr(&arr, (uint64_t)(i * 5), &i);
    EXPECT(sn_darray_get_length(arr) == 16, "Expected length to be 16");
    sn_darray_destroy(arr);
}

static void test_shrink_to_fit(void) {
    int *arr = sn_darray_create(int, NULL);
    for (int i = 0; i < 15; ++i) sn_darray_push(&arr, i);
    EXPECT(sn_darray_get_length(arr) == 15, "Expected length to be 15");
    EXPECT(sn_darray_get_capacity(arr) == 20, "Expected capacity to be 20 before shrink to fit");
    sn_darray_shrink_to_fit(&arr);
    EXPECT(sn_darray_get_capacity(arr) == 15, "Expected capacity to be 15 (=lenght) after shrink");
    sn_darray_destroy(arr);
}

static void test_shrink_to_fit_empty(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_shrink_to_fit(&arr);
    EXPECT(sn_darray_get_capacity(arr) == 1, "empty shrink keeps a capacity of one");
    EXPECT(sn_darray_get_length(arr) == 0, "empty shrink keeps the length at zero");

    sn_darray_push(&arr, 42);
    EXPECT(sn_darray_get_length(arr) == 1, "push after an empty shrink succeeds");
    EXPECT(arr[0] == 42, "push after an empty shrink stores the value");
    sn_darray_destroy(arr);
}

static void test_resize_to_zero(void) {
    int *arr = sn_darray_create(int, NULL);
    sn_darray_push(&arr, 1);
    sn_darray_resize(&arr, 0);
    EXPECT(sn_darray_get_capacity(arr) == 1, "resize to zero keeps a capacity of one");
    EXPECT(sn_darray_get_length(arr) == 0, "resize to zero clears the length");

    sn_darray_push(&arr, 7);
    EXPECT(sn_darray_get_length(arr) == 1, "push after resize to zero succeeds");
    EXPECT(arr[0] == 7, "push after resize to zero stores the value");
    sn_darray_destroy(arr);
}

static void test_push_elements(void) {
    int *arr = sn_darray_create(int, NULL);
    int values[] = {1, 2, 3, 4, 5, 6};
    sn_darray_push_elements(&arr, values, UINT64_C(6));
    EXPECT(sn_darray_get_length(arr) == UINT64_C(6), "length after a multi push");
    EXPECT(sn_darray_get_capacity(arr) >= UINT64_C(6), "capacity covers a multi push");
    for (int i = 0; i < 6; ++i) EXPECT(arr[i] == i + 1, "multi push preserves order");

    sn_darray_push_elements(&arr, values, UINT64_C(6));
    EXPECT(sn_darray_get_length(arr) == UINT64_C(12), "length after a growing multi push");
    for (int i = 0; i < 12; ++i)
        EXPECT(arr[i] == (i % 6) + 1, "values survive a growing multi push");
    sn_darray_destroy(arr);
}

static void test_push_elements_at(void) {
    int *arr = sn_darray_create(int, NULL);
    for (int i = 0; i < 3; ++i) sn_darray_push(&arr, i);

    int middle[] = {10, 11};
    sn_darray_push_elements_at(&arr, UINT64_C(1), middle, UINT64_C(2));
    EXPECT(sn_darray_get_length(arr) == UINT64_C(5), "length after a multi insert");
    int expected_middle[] = {0, 10, 11, 1, 2};
    for (int i = 0; i < 5; ++i)
        EXPECT(arr[i] == expected_middle[i], "multi insert preserves order");

    int tail[] = {20, 21};
    sn_darray_push_elements_at(&arr, UINT64_C(5), tail, UINT64_C(2));
    EXPECT(sn_darray_get_length(arr) == UINT64_C(7), "length after a multi append");
    EXPECT(arr[5] == 20 && arr[6] == 21, "multi append stores the elements");

    int far[] = {30, 31};
    sn_darray_push_elements_at(&arr, UINT64_C(10), far, UINT64_C(2));
    EXPECT(sn_darray_get_length(arr) == UINT64_C(12), "length after a gap multi insert");
    EXPECT(arr[8] == 0 && arr[9] == 0, "gap slots are zeroed");
    EXPECT(arr[10] == 30 && arr[11] == 31, "gap multi insert stores the elements");
    sn_darray_destroy(arr);
}

static void test_push_zero_elements(void) {
    int *arr = sn_darray_create(int, NULL);
    int values[] = {1, 2, 3};
    uint64_t capacity = sn_darray_get_capacity(arr);

    sn_darray_push_elements(&arr, values, 0);
    EXPECT(sn_darray_get_length(arr) == 0, "a zero length push does not change the length");
    EXPECT(sn_darray_get_capacity(arr) == capacity, "a zero length push does not grow the array");

    sn_darray_push_elements_at(&arr, UINT64_C(5), values, 0);
    EXPECT(sn_darray_get_length(arr) == 0, "a zero length insert does not change the length");
    EXPECT(sn_darray_get_capacity(arr) == capacity, "a zero length insert does not grow the array");
    sn_darray_destroy(arr);
}

static void test_alloc_failure_create(void) {
    TestAllocData data = {0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};
    data.fail_alloc_call = 1;

    int *arr = sn_darray_create(int, &alloc);
    EXPECT(arr == NULL, "create returns NULL when the allocator fails");
    sn_darray_destroy(arr);
}

static void test_alloc_failure_grow(void) {
    TestAllocData data = {0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    int *arr = sn_darray_create_with_capacity(2, int, &alloc);
    EXPECT(arr != NULL, "create with a custom allocator");
    sn_darray_push(&arr, 10);
    sn_darray_push(&arr, 20);

    uint64_t length = sn_darray_get_length(arr);
    uint64_t capacity = sn_darray_get_capacity(arr);

    data.fail_realloc_call = data.realloc_calls + 1;
    EXPECT(!sn_darray_push(&arr, 30), "push reports a failed growth");
    EXPECT(sn_darray_get_length(arr) == length, "failed push keeps the length");
    EXPECT(sn_darray_get_capacity(arr) == capacity, "failed push keeps the capacity");
    EXPECT(arr[0] == 10 && arr[1] == 20, "failed push keeps the elements");

    data.fail_realloc_call = 0;
    EXPECT(sn_darray_push(&arr, 30), "push succeeds once the allocator recovers");
    EXPECT(sn_darray_get_length(arr) == length + 1, "the retried push appends");
    EXPECT(arr[2] == 30, "the retried value is stored");
    sn_darray_destroy(arr);
}

static void test_alloc_failure_resize_and_reserve(void) {
    TestAllocData data = {0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    int *arr = sn_darray_create_with_capacity(2, int, &alloc);
    sn_darray_push(&arr, 7);

    data.fail_realloc_call = data.realloc_calls + 1;
    EXPECT(!sn_darray_resize(&arr, UINT64_C(8)), "resize reports a failed allocation");
    EXPECT(sn_darray_get_capacity(arr) == 2, "failed resize keeps the capacity");
    EXPECT(sn_darray_get_length(arr) == 1, "failed resize keeps the length");
    EXPECT(arr[0] == 7, "failed resize keeps the elements");

    data.fail_realloc_call = data.realloc_calls + 1;
    EXPECT(!sn_darray_reserve(&arr, UINT64_C(8)), "reserve reports a failed allocation");
    EXPECT(sn_darray_get_capacity(arr) == 2, "failed reserve keeps the capacity");

    data.fail_realloc_call = 0;
    EXPECT(sn_darray_reserve(&arr, UINT64_C(8)), "reserve succeeds once the allocator recovers");
    EXPECT(sn_darray_get_capacity(arr) >= UINT64_C(8), "reserve guarantees the capacity");
    EXPECT(sn_darray_get_length(arr) == 1, "reserve does not change the length");
    EXPECT(arr[0] == 7, "reserve does not change the elements");
    sn_darray_destroy(arr);
}

static void test_alloc_failure_shrink(void) {
    TestAllocData data = {0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    int *arr = sn_darray_create_with_capacity(8, int, &alloc);
    sn_darray_push(&arr, 1);
    sn_darray_push(&arr, 2);

    data.fail_realloc_call = data.realloc_calls + 1;
    EXPECT(!sn_darray_shrink_to_fit(&arr), "shrink reports a failed allocation");
    EXPECT(sn_darray_get_capacity(arr) == 8, "failed shrink keeps the capacity");
    EXPECT(sn_darray_get_length(arr) == 2, "failed shrink keeps the length");

    data.fail_realloc_call = 0;
    EXPECT(sn_darray_shrink_to_fit(&arr), "shrink succeeds once the allocator recovers");
    EXPECT(sn_darray_get_capacity(arr) == 2, "shrink sets the capacity to the length");
    EXPECT(sn_darray_get_length(arr) == 2, "shrink keeps the length");
    EXPECT(arr[0] == 1 && arr[1] == 2, "shrink keeps the elements");
    sn_darray_destroy(arr);
}

static void test_alloc_failure_push_at(void) {
    TestAllocData data = {0};
    SnMemoryAllocator alloc = {test_alloc, test_realloc, test_free, &data};

    int *arr = sn_darray_create_with_capacity(2, int, &alloc);
    sn_darray_push(&arr, 1);
    sn_darray_push(&arr, 2);

    data.fail_realloc_call = data.realloc_calls + 1;
    EXPECT(!sn_darray_push_at(&arr, 1, 9), "push_at reports a failed growth");
    EXPECT(sn_darray_get_length(arr) == 2, "failed push_at keeps the length");
    EXPECT(arr[0] == 1 && arr[1] == 2, "failed push_at keeps the elements");

    data.fail_realloc_call = 0;
    EXPECT(sn_darray_push_at(&arr, 1, 9), "push_at succeeds once the allocator recovers");
    EXPECT(sn_darray_get_length(arr) == 3, "the retried push_at appends");
    EXPECT(arr[0] == 1 && arr[1] == 9 && arr[2] == 2, "the retried push_at inserts");
    sn_darray_destroy(arr);
}

/* -- main ---------------------------------------------------------------- */

int main(void) {
    test_create_destroy();
    test_create_with_capacity();
    test_push_and_length();
    test_resize_on_push();
    test_pop_value();
    test_pop_null();
    test_pop_empty();
    test_push_at_middle();
    test_push_at_end();
    test_push_at_beyond_end();
    test_pop_at();
    test_pop_at_first();
    test_pop_at_null();
    test_clear();
    test_custom_allocator();
    test_different_types();
    test_large_count();
    test_push_from_ptr();
    test_shrink_to_fit();
    test_shrink_to_fit_empty();
    test_resize_to_zero();
    test_push_elements();
    test_push_elements_at();
    test_push_zero_elements();
    test_alloc_failure_create();
    test_alloc_failure_grow();
    test_alloc_failure_resize_and_reserve();
    test_alloc_failure_shrink();
    test_alloc_failure_push_at();
    return failed;
}
