add_executable(grapple_app_tests
  NativeProjectSessionTest.cpp
)

target_link_libraries(grapple_app_tests
  PRIVATE
    Grapple::App
)

target_include_directories(grapple_app_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_app_tests COMMAND grapple_app_tests)
