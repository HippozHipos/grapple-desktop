add_executable(grapple_foundation_tests
  FoundationTest.cpp
)

target_link_libraries(grapple_foundation_tests
  PRIVATE
    Grapple::Foundation
)

target_include_directories(grapple_foundation_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_foundation_tests COMMAND grapple_foundation_tests)

