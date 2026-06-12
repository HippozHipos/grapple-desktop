add_library(grapple_storage
  src/ProjectPackageStore.cpp
)

add_library(Grapple::Storage ALIAS grapple_storage)

target_include_directories(grapple_storage
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_storage
  PUBLIC
    Grapple::Foundation
    Grapple::Project
    Grapple::History
)

