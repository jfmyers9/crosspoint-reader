add_executable(HttpDownloadHeapTest HttpDownloadHeapTest.cpp)

target_include_directories(HttpDownloadHeapTest PRIVATE
  mocks
  ${REPO_ROOT}/src/network
)

target_link_libraries(HttpDownloadHeapTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HttpDownloadHeapTest)
