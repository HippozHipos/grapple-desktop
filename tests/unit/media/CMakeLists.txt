find_package(OpenCV REQUIRED COMPONENTS core imgcodecs videoio)

add_executable(grapple_media_frame_cache_tests
  FrameCacheTest.cpp
)

target_link_libraries(grapple_media_frame_cache_tests
  PRIVATE
    Grapple::Media
)

target_include_directories(grapple_media_frame_cache_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_media_frame_cache_tests COMMAND grapple_media_frame_cache_tests)

add_executable(grapple_media_caching_reader_tests
  CachingMediaReaderTest.cpp
)

target_link_libraries(grapple_media_caching_reader_tests
  PRIVATE
    Grapple::Media
)

target_include_directories(grapple_media_caching_reader_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_media_caching_reader_tests COMMAND grapple_media_caching_reader_tests)

add_executable(grapple_media_opencv_reader_tests
  OpenCVMediaReaderTest.cpp
)

target_link_libraries(grapple_media_opencv_reader_tests
  PRIVATE
    Grapple::Media
    ${OpenCV_LIBS}
)

target_include_directories(grapple_media_opencv_reader_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${OpenCV_INCLUDE_DIRS}
)

add_test(NAME grapple_media_opencv_reader_tests COMMAND grapple_media_opencv_reader_tests)
