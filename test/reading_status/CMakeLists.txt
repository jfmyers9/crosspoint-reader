add_executable(ReadingStatusTest ReadingStatusTest.cpp ${REPO_ROOT}/lib/ReadingStatus/ReadingStatus.cpp)
target_include_directories(ReadingStatusTest PRIVATE stubs ${REPO_ROOT}/lib/ReadingStatus)
target_link_libraries(ReadingStatusTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingStatusTest)
