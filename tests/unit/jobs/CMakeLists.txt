add_executable(grapple_jobs_tests
  JobsTest.cpp
)

target_link_libraries(grapple_jobs_tests
  PRIVATE
    Grapple::Jobs
)

target_include_directories(grapple_jobs_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_jobs_tests COMMAND grapple_jobs_tests)

