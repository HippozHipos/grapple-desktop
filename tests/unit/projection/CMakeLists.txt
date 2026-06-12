add_executable(grapple_projection_tests
  ProjectionTest.cpp
)

target_link_libraries(grapple_projection_tests
  PRIVATE
    Grapple::Projection
    Grapple::Project
)

target_include_directories(grapple_projection_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_projection_tests COMMAND grapple_projection_tests)

