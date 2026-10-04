FetchContent_GetProperties(arduinojson)
add_executable(BookOrbitHighlightReaderTest
  BookOrbitHighlightReaderTest.cpp
  ${REPO_ROOT}/lib/BookOrbit/BookOrbitHighlightReader.cpp
  ${REPO_ROOT}/src/util/BookmarkUtil.cpp
)
target_include_directories(BookOrbitHighlightReaderTest PRIVATE
  stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/BookOrbit
  ${arduinojson_SOURCE_DIR}/src
)
target_link_libraries(BookOrbitHighlightReaderTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookOrbitHighlightReaderTest)
