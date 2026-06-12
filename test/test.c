#include <sncontainer/darray.h>

int main(void) {
    int *arr = sn_darray_create(int, NULL);
    for (uint64_t i = 0; i < 100; ++i) {
        sn_darray_push(&arr, i);
    }
    if (sn_darray_get_length(arr) != 100) return 1;
    sn_darray_destroy(arr);

    return 0;
}
