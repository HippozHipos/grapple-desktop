add_executable(grapple_render_tests
  PreviewSessionTest.cpp
)

target_link_libraries(grapple_render_tests
  PRIVATE
    Grapple::Render
)

target_include_directories(grapple_render_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_render_tests COMMAND grapple_render_tests)

