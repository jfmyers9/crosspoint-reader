add_executable(LocalReadingStatsTest
  LocalReadingStatsTest.cpp
  ${REPO_ROOT}/lib/ReadingStats/LocalReadingStatsModel.cpp
  ${REPO_ROOT}/lib/ReadingStats/ReadingStatsRecorder.cpp
)
target_include_directories(LocalReadingStatsTest PRIVATE ${REPO_ROOT}/lib/ReadingStats)
target_link_libraries(LocalReadingStatsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LocalReadingStatsTest)

add_executable(LocalReadingStatsStorageTest
  LocalReadingStatsStorageTest.cpp
  ${REPO_ROOT}/lib/ReadingStats/LocalReadingStats.cpp
  ${REPO_ROOT}/lib/ReadingStats/LocalReadingStatsModel.cpp
  ${REPO_ROOT}/lib/ReadingStats/ReadingStatsRecorder.cpp
)
target_compile_definitions(LocalReadingStatsStorageTest PRIVATE CROSSPOINT_ENABLE_LOCAL_READING_STATS)
target_include_directories(LocalReadingStatsStorageTest PRIVATE stubs ${REPO_ROOT}/lib/ReadingStats)
target_link_libraries(LocalReadingStatsStorageTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LocalReadingStatsStorageTest)
