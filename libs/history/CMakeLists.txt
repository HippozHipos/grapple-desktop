find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

add_library(grapple_history
  src/CommandLogStore.cpp
  src/EventLogStore.cpp
  src/HistorySerializer.cpp
  src/SnapshotStore.cpp
)

add_library(Grapple::History ALIAS grapple_history)

target_include_directories(grapple_history
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_directories(grapple_history
  PRIVATE
    ${JSONCPP_LIBRARY_DIRS}
)

target_link_libraries(grapple_history
  PUBLIC
    Grapple::Foundation
  PRIVATE
    ${JSONCPP_LIBRARIES}
)
