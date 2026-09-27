enable_testing()

add_executable(sn_container_test test.c)
target_link_libraries(sn_container_test PRIVATE sncontainer)

add_test(NAME sn_container_test COMMAND sn_container_test)

add_executable(sn_sparse_set_test sparse_set_test.c)
target_link_libraries(sn_sparse_set_test PRIVATE sncontainer)

add_test(NAME sn_sparse_set_test COMMAND sn_sparse_set_test)

add_executable(sn_key_allocator_test key_allocator_test.c)
target_link_libraries(sn_key_allocator_test PRIVATE sncontainer)

add_test(NAME sn_key_allocator_test COMMAND sn_key_allocator_test)
