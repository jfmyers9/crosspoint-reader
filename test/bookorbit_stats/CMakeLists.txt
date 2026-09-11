add_executable(BookOrbitStatsTest
  BookOrbitStatsTest.cpp
  ${REPO_ROOT}/lib/BookOrbit/BookOrbitStats.cpp
  ${REPO_ROOT}/lib/ReadingStats/ReadingStatsRecorder.cpp
  ${REPO_ROOT}/lib/ReadingStats/ReadingStatsOutbox.cpp
)
target_compile_definitions(BookOrbitStatsTest PRIVATE CROSSPOINT_ENABLE_BOOKORBIT_STATS)
target_include_directories(BookOrbitStatsTest PRIVATE
  stubs
  ${REPO_ROOT}/lib/BookOrbit
  ${REPO_ROOT}/lib/ReadingStats
)
target_link_libraries(BookOrbitStatsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookOrbitStatsTest)
