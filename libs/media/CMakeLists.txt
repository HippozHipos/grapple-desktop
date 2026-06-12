find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc videoio)

add_library(grapple_media
  src/CachingMediaReader.cpp
  src/FrameCache.cpp
  src/MediaSource.cpp
  src/OpenCVMediaReader.cpp
)

add_library(Grapple::Media ALIAS grapple_media)

target_include_directories(grapple_media
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_media
  PUBLIC
    Grapple::Foundation
  PRIVATE
    ${OpenCV_LIBS}
)

target_include_directories(grapple_media
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)
