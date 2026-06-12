add_library(grapple_agent
  src/AgentToolRegistry.cpp
  src/ProjectTools.cpp
)

add_library(Grapple::Agent ALIAS grapple_agent)

target_include_directories(grapple_agent
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_agent
  PUBLIC
    Grapple::Foundation
    Grapple::Project
    Grapple::Model
    Grapple::History
)

