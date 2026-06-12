add_library(grapple_history
  src/CommandLogStore.cpp
  src/EventLogStore.cpp
  src/SnapshotStore.cpp
)

add_library(Grapple::History ALIAS grapple_history)

target_include_directories(grapple_history
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_history
  PUBLIC
    Grapple::Foundation
)

