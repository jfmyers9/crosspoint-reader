find_path(BOOKORBIT_ARDUINOJSON_INCLUDE ArduinoJson.h
  PATHS "${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src"
        "${REPO_ROOT}/.pio/libdeps/x4pro/ArduinoJson/src")
if(NOT BOOKORBIT_ARDUINOJSON_INCLUDE)
  FetchContent_Declare(annotations_arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2)
  FetchContent_MakeAvailable(annotations_arduinojson)
  set(BOOKORBIT_ARDUINOJSON_INCLUDE "${annotations_arduinojson_SOURCE_DIR}/src")
endif()
add_executable(BookOrbitAnnotationsClientTest
  BookOrbitAnnotationsClientTest.cpp
  ${REPO_ROOT}/lib/BookOrbit/BookOrbitAnnotationsClient.cpp)
target_include_directories(BookOrbitAnnotationsClientTest PRIVATE
  stubs ${REPO_ROOT}/lib/BookOrbit
  ${BOOKORBIT_ARDUINOJSON_INCLUDE})
target_compile_definitions(BookOrbitAnnotationsClientTest PRIVATE CROSSPOINT_ENABLE_BOOKORBIT_STATS)
target_link_libraries(BookOrbitAnnotationsClientTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookOrbitAnnotationsClientTest)
