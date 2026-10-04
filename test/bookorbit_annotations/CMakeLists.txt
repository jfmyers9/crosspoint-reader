add_executable(BookOrbitAnnotationsTest
  BookOrbitAnnotationsTest.cpp
  ${REPO_ROOT}/lib/BookOrbit/BookOrbitAnnotations.cpp)
target_include_directories(BookOrbitAnnotationsTest PRIVATE
  stubs ../bookorbit_stats/stubs ${REPO_ROOT}/lib/BookOrbit ${REPO_ROOT}/src)
target_compile_definitions(BookOrbitAnnotationsTest PRIVATE CROSSPOINT_ENABLE_BOOKORBIT_STATS)
target_link_libraries(BookOrbitAnnotationsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookOrbitAnnotationsTest)
