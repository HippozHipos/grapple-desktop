add_library(grapple_foundation
  src/Hash.cpp
  src/Json.cpp
)

add_library(Grapple::Foundation ALIAS grapple_foundation)

target_include_directories(grapple_foundation
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
