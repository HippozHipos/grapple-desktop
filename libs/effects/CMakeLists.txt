add_library(grapple_effects INTERFACE)

add_library(Grapple::Effects ALIAS grapple_effects)

target_include_directories(grapple_effects
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
