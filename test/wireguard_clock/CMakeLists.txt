add_executable(WireGuardClockTest
  WireGuardClockTest.cpp
  ${REPO_ROOT}/lib/TailscaleTransport/WireGuardClock.cpp
)
target_compile_definitions(WireGuardClockTest PRIVATE CROSSPOINT_ENABLE_TAILSCALE=1)
target_include_directories(WireGuardClockTest PRIVATE
  mocks
  ${REPO_ROOT}/lib/TailscaleTransport
)
target_link_libraries(WireGuardClockTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WireGuardClockTest)
