add_executable(ReadingStatsOutboxTest
  ReadingStatsOutboxTest.cpp
  ${REPO_ROOT}/lib/ReadingStats/ReadingStatsOutbox.cpp
)
target_include_directories(ReadingStatsOutboxTest PRIVATE
  stubs
  ${REPO_ROOT}/lib/ReadingStats
)
target_link_libraries(ReadingStatsOutboxTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(ReadingStatsOutboxTest)
