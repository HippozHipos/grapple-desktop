find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

add_library(grapple_project
  src/ProjectDeserializer.cpp
  src/ProjectCommand.cpp
  src/ProjectCommandNames.cpp
  src/ProjectController.cpp
  src/ProjectEventNames.cpp
  src/ProjectEvents.cpp
  src/ProjectQuery.cpp
  src/ProjectSerializer.cpp
  src/ProjectSnapshot.cpp
)

add_library(Grapple::Project ALIAS grapple_project)

target_include_directories(grapple_project
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_directories(grapple_project
  PRIVATE
    ${JSONCPP_LIBRARY_DIRS}
)

target_link_libraries(grapple_project
  PUBLIC
    Grapple::Foundation
    Grapple::Asset
    Grapple::Graph
    Grapple::Timeline
  PRIVATE
    ${JSONCPP_LIBRARIES}
)
