add_library(grapple_project
  src/ProjectCommand.cpp
  src/ProjectCommandNames.cpp
  src/ProjectController.cpp
  src/ProjectEventNames.cpp
  src/ProjectEvents.cpp
  src/ProjectSerializer.cpp
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
    Grapple::Timeline
)
