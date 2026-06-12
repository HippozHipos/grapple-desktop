add_executable(grapple_storage_tests
  ProjectPackageStoreTest.cpp
)

target_link_libraries(grapple_storage_tests
  PRIVATE
    Grapple::Storage
)

target_include_directories(grapple_storage_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_storage_tests COMMAND grapple_storage_tests)

