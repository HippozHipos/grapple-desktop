add_executable(grapple_model_tests
  ModelRegistryTest.cpp
)

target_link_libraries(grapple_model_tests
  PRIVATE
    Grapple::Model
)

target_include_directories(grapple_model_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_model_tests COMMAND grapple_model_tests)

