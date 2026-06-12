add_library(grapple_timeline
  src/ParamValue.cpp
)

add_library(Grapple::Timeline ALIAS grapple_timeline)

target_include_directories(grapple_timeline
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_timeline
  PUBLIC
    Grapple::Foundation
)

