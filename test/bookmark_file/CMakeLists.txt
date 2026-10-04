# Keep the JSON implementation/version identical to platformio.ini.
FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
)
FetchContent_MakeAvailable(arduinojson)

add_executable(BookmarkFileTest
  BookmarkFileTest.cpp
  ${REPO_ROOT}/src/util/BookmarkFile.cpp
  ${REPO_ROOT}/src/util/BookmarkUtil.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
)
target_include_directories(BookmarkFileTest PRIVATE
  stubs
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/Serialization
  ${arduinojson_SOURCE_DIR}/src
)
target_compile_definitions(BookmarkFileTest PRIVATE ARDUINOJSON_ENABLE_ARDUINO_STRING=1)
target_link_libraries(BookmarkFileTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookmarkFileTest)
