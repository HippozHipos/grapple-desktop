find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

add_library(grapple_storage
  src/ProjectCommitBuilder.cpp
  src/ProjectPackageManifest.cpp
  src/ProjectPackageReader.cpp
  src/ProjectPackageSession.cpp
  src/ProjectPackageStore.cpp
  src/ProjectPackageWriter.cpp
)

add_library(Grapple::Storage ALIAS grapple_storage)

target_include_directories(grapple_storage
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_directories(grapple_storage
  PRIVATE
    ${JSONCPP_LIBRARY_DIRS}
)

target_link_libraries(grapple_storage
  PUBLIC
    Grapple::Foundation
    Grapple::Project
    Grapple::History
  PRIVATE
    ${JSONCPP_LIBRARIES}
)
