add_executable(grapple_project_tests
  ProjectControllerTest.cpp
)

target_link_libraries(grapple_project_tests
  PRIVATE
    Grapple::Project
)

target_include_directories(grapple_project_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_project_tests COMMAND grapple_project_tests)

