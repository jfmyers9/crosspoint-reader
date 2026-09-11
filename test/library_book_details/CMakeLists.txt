add_executable(LibraryBookDetailsTest LibraryBookDetailsTest.cpp
  ${REPO_ROOT}/src/util/LibraryBookDetails.cpp)
target_include_directories(LibraryBookDetailsTest PRIVATE
  stubs ${REPO_ROOT}/src/util ${REPO_ROOT}/lib/Memory)
target_link_libraries(LibraryBookDetailsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryBookDetailsTest)
