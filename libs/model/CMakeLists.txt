add_library(grapple_model
  src/ModelRegistry.cpp
)

add_library(Grapple::Model ALIAS grapple_model)

target_include_directories(grapple_model
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_model
  PUBLIC
    Grapple::Foundation
)

