find_package(Threads REQUIRED)
add_executable(LibraryCoverLoaderTest LibraryCoverLoaderTest.cpp
  ${REPO_ROOT}/src/util/LibraryCoverLoader.cpp)
target_include_directories(LibraryCoverLoaderTest PRIVATE stubs ${REPO_ROOT}/src/util)
target_link_libraries(LibraryCoverLoaderTest PRIVATE crosspoint_test_common GTest::gtest_main Threads::Threads)
gtest_discover_tests(LibraryCoverLoaderTest)
