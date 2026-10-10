FetchContent_GetProperties(arduinojson)
add_executable(BookOrbitHighlightReaderTest
  BookOrbitHighlightReaderTest.cpp
  ${REPO_ROOT}/lib/BookOrbit/BookOrbitHighlightReader.cpp
  ${REPO_ROOT}/src/util/BookmarkUtil.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(BookOrbitHighlightReaderTest PRIVATE
  stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/BookOrbit
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
  ${arduinojson_SOURCE_DIR}/src
)
target_link_libraries(BookOrbitHighlightReaderTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookOrbitHighlightReaderTest)
