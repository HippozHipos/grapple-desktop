add_library(grapple_graph
  src/GraphDocument.cpp
  src/GraphDiff.cpp
  src/GraphSerializer.cpp
)

add_library(Grapple::Graph ALIAS grapple_graph)

target_include_directories(grapple_graph
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_graph
  PUBLIC
    Grapple::Foundation
    Grapple::Timeline
)
