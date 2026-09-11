find_path(LIBRARY_LAYOUT_ARDUINOJSON_INCLUDE ArduinoJson.h
  PATHS "${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src"
        "${REPO_ROOT}/.pio/libdeps/x4pro/ArduinoJson/src")
if(NOT LIBRARY_LAYOUT_ARDUINOJSON_INCLUDE)
  FetchContent_Declare(library_layout_arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2)
  FetchContent_MakeAvailable(library_layout_arduinojson)
  set(LIBRARY_LAYOUT_ARDUINOJSON_INCLUDE "${library_layout_arduinojson_SOURCE_DIR}/src")
endif()
add_executable(LibraryLayoutSettingsTest LibraryLayoutSettingsTest.cpp)
target_include_directories(LibraryLayoutSettingsTest PRIVATE
  ${REPO_ROOT}/src ${LIBRARY_LAYOUT_ARDUINOJSON_INCLUDE})
target_link_libraries(LibraryLayoutSettingsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LibraryLayoutSettingsTest)
