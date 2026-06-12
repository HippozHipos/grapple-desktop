add_executable(grapple_native_flow_tests
  NativeFlowTest.cpp
)

target_link_libraries(grapple_native_flow_tests
  PRIVATE
    Grapple::Project
    Grapple::Projection
    Grapple::Runtime
    Grapple::Render
    Grapple::Storage
)

target_include_directories(grapple_native_flow_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_native_flow_tests COMMAND grapple_native_flow_tests)
