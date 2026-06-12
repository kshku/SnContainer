enable_testing()

add_executable(sn_container_test test.c)
target_link_libraries(sn_container_test PRIVATE sncontainer)

add_test(NAME sn_container_test COMMAND sn_container_test)
