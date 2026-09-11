add_executable(HalClockUtcTest
  HalClockUtcTest.cpp
  ${REPO_ROOT}/lib/hal/HalClock.cpp
)
target_include_directories(HalClockUtcTest PRIVATE
  stubs
  ${REPO_ROOT}/lib/hal
)
target_link_libraries(HalClockUtcTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HalClockUtcTest)
