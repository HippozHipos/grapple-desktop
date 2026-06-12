add_library(grapple_render
  src/LocalRenderSystem.cpp
)

add_library(Grapple::Render ALIAS grapple_render)

target_include_directories(grapple_render
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_render
  PUBLIC
    Grapple::Foundation
    Grapple::Projection
    Grapple::Runtime
)
