if(SN_CONTAINER_BUILD_SHARED)
    add_library(sncontainer SHARED)
    target_compile_definitions(sncontainer PRIVATE SN_EXPORT)
else()
    add_library(sncontainer STATIC)
    target_compile_definitions(sncontainer PUBLIC SN_CONTAINER_STATIC)
endif()

target_include_directories(sncontainer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(sncontainer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(sncontainer PRIVATE sn_container_configs)

add_subdirectory(src)
