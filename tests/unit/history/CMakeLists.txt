add_executable(grapple_history_tests
  HistoryStoreTest.cpp
)

target_link_libraries(grapple_history_tests
  PRIVATE
    Grapple::History
)

target_include_directories(grapple_history_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_history_tests COMMAND grapple_history_tests)

