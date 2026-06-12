add_library(grapple_project
  src/ProjectController.cpp
)

add_library(Grapple::Project ALIAS grapple_project)

target_include_directories(grapple_project
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_project
  PUBLIC
    Grapple::Foundation
    Grapple::Graph
)

