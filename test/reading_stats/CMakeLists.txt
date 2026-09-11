add_executable(ReadingStatsRecorderTest
  ReadingStatsRecorderTest.cpp
  ${REPO_ROOT}/lib/ReadingStats/ReadingStatsRecorder.cpp
)
target_include_directories(ReadingStatsRecorderTest PRIVATE ${REPO_ROOT}/lib/ReadingStats)
target_link_libraries(ReadingStatsRecorderTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingStatsRecorderTest)
