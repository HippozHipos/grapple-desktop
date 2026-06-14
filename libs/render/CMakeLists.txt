add_library(grapple_render
  src/FinalRenderShell.cpp
  src/LocalRenderCore.cpp
  src/LocalRenderSystem.cpp
  src/PreviewRenderShell.cpp
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
