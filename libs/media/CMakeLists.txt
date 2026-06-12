add_library(grapple_media
  src/FrameCache.cpp
)

add_library(Grapple::Media ALIAS grapple_media)

target_include_directories(grapple_media
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_media
  PUBLIC
    Grapple::Foundation
)

