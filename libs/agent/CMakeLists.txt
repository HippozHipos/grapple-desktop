find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

add_library(grapple_agent
  src/AgentBridge.cpp
  src/AgentConversationState.cpp
  src/AgentRunEvent.cpp
  src/AgentToolRegistry.cpp
  src/ProjectTools.cpp
)

add_library(Grapple::Agent ALIAS grapple_agent)

target_include_directories(grapple_agent
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_directories(grapple_agent
  PRIVATE
    ${JSONCPP_LIBRARY_DIRS}
)

target_link_libraries(grapple_agent
  PUBLIC
    Grapple::Foundation
    Grapple::Project
    Grapple::Model
    Grapple::History
  PRIVATE
    ${JSONCPP_LIBRARIES}
)
