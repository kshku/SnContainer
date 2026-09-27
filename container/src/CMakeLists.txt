set(HEADERFILES
    api.h
    darray.h
    sparse_set.h
)

set(SRCS
    darray.c
    sparse_set.c
)

set(INCLUDE_BASE "${PROJECT_SOURCE_DIR}/container/include/sncontainer")

list(TRANSFORM HEADERFILES PREPEND "${INCLUDE_BASE}/")

target_sources(sncontainer PRIVATE ${HEADERFILES} ${SRCS})
