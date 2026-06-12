add_executable(grapple_graph_tests
  GraphDocumentTest.cpp
)

target_link_libraries(grapple_graph_tests
  PRIVATE
    Grapple::Graph
)

target_include_directories(grapple_graph_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_graph_tests COMMAND grapple_graph_tests)

