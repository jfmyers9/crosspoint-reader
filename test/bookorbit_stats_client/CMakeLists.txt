find_path(BOOKORBIT_ARDUINOJSON_INCLUDE ArduinoJson.h
  PATHS "${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src"
        "${REPO_ROOT}/.pio/libdeps/x4pro/ArduinoJson/src")
if(NOT BOOKORBIT_ARDUINOJSON_INCLUDE)
  FetchContent_Declare(bookorbit_arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2)
  FetchContent_MakeAvailable(bookorbit_arduinojson)
  set(BOOKORBIT_ARDUINOJSON_INCLUDE "${bookorbit_arduinojson_SOURCE_DIR}/src")
endif()
add_executable(BookOrbitStatsClientTest
  BookOrbitStatsClientTest.cpp
  ${REPO_ROOT}/lib/BookOrbit/BookOrbitStatsClient.cpp)
target_include_directories(BookOrbitStatsClientTest PRIVATE
  stubs ${REPO_ROOT}/lib/BookOrbit ${REPO_ROOT}/lib/ReadingStats
  ${BOOKORBIT_ARDUINOJSON_INCLUDE})
target_compile_definitions(BookOrbitStatsClientTest PRIVATE CROSSPOINT_ENABLE_BOOKORBIT_STATS)
target_link_libraries(BookOrbitStatsClientTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookOrbitStatsClientTest)
