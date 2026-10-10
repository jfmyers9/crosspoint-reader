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

add_executable(HttpDownloadAttemptTest
  HttpDownloadAttemptTest.cpp
  ${REPO_ROOT}/src/network/HttpDownloader.cpp
)
target_include_directories(HttpDownloadAttemptTest PRIVATE
  mocks
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/Memory
)
target_compile_definitions(HttpDownloadAttemptTest PRIVATE CROSSPOINT_VERSION="test")
target_link_libraries(HttpDownloadAttemptTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HttpDownloadAttemptTest)

add_executable(HttpTailnetFeedTest
  HttpTailnetFeedTest.cpp
  ${REPO_ROOT}/src/network/HttpDownloader.cpp
)
target_include_directories(HttpTailnetFeedTest PRIVATE
  mocks
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/Memory
)
target_compile_definitions(HttpTailnetFeedTest PRIVATE CROSSPOINT_VERSION="test" CROSSPOINT_ENABLE_TAILSCALE=1)
target_link_libraries(HttpTailnetFeedTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HttpTailnetFeedTest)
