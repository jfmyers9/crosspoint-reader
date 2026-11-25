add_executable(LinkBackStackTest
  LinkBackStackTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressComparison.cpp
)

target_include_directories(LinkBackStackTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(LinkBackStackTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LinkBackStackTest)
