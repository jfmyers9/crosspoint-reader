add_executable(LibraryOpfParserTest
  LibraryOpfParserTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
)
target_include_directories(LibraryOpfParserTest PRIVATE
  stubs
  ${REPO_ROOT}/test/koreader_xpath_resolver/stubs
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/XmlParserUtils
)
target_link_libraries(LibraryOpfParserTest PRIVATE crosspoint_test_common GTest::gtest_main expat)
gtest_discover_tests(LibraryOpfParserTest)
