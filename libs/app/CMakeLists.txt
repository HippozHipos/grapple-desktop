add_library(grapple_app
  src/NativeProjectSession.cpp
)

add_library(Grapple::App ALIAS grapple_app)

target_include_directories(grapple_app
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_app
  PUBLIC
    Grapple::Foundation
    Grapple::Project
    Grapple::Projection
    Grapple::Storage
)
