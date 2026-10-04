add_executable(MappedInputEdgesTest MappedInputEdgesTest.cpp)
target_include_directories(MappedInputEdgesTest PRIVATE stubs ../../src)
target_link_libraries(MappedInputEdgesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(MappedInputEdgesTest)
