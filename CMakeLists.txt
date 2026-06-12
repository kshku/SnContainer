cmake_minimum_required(VERSION 3.15...3.31)

project(SnContainer VERSION 0.0.0 DESCRIPTION "Containers" LANGUAGES C)

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)

include(FetchContent)
FetchContent_Declare(sncore
    GIT_REPOSITORY https://github.com/kshku/SnCore.git
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(sncore)

add_library(sn_container_configs INTERFACE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(sn_container_configs INTERFACE SN_DEBUG)
endif()

set(gcc_clang_comp "$<COMPILE_LANG_AND_ID:C,Clang,GNU>")
set(msvc_comp "$<COMPILE_LANG_AND_ID:C,MSVC>")

target_compile_options(sn_container_configs INTERFACE "$<${gcc_clang_comp}:-Wall;-Wextra;-Wformat;-Wuninitialized;-Wpedantic>")

target_compile_options(sn_container_configs INTERFACE "$<${gcc_clang_comp}:-fvisibility=hidden>")

target_compile_definitions(sn_container_configs INTERFACE
    SN_CONTAINER_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SN_CONTAINER_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SN_CONTAINER_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

option(SN_CONTAINER_BUILD_SHARED "Build shared library" OFF)
option(SN_CONTAINER_BUILD_TEST "Build tests" OFF)

add_subdirectory(docs)
add_subdirectory(container)

if(SN_CONTAINER_BUILD_TEST)
    add_subdirectory("test")
else()
    message(STATUS "Building test is disabled")
endif()
