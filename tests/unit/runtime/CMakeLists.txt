add_executable(grapple_runtime_tests
  RuntimeTest.cpp
)

target_link_libraries(grapple_runtime_tests
  PRIVATE
    Grapple::Runtime
)

target_include_directories(grapple_runtime_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_runtime_tests COMMAND grapple_runtime_tests)

