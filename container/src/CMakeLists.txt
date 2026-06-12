set(HEADERFILES
    api.h
    darray.h
)

set(SRCS
    darray.c
)

set(INCLUDE_BASE "${PROJECT_SOURCE_DIR}/container/include/sncontainer")

list(TRANSFORM HEADERFILES PREPEND "${INCLUDE_BASE}/")

target_sources(sncontainer PRIVATE ${HEADERFILES} ${SRCS})
