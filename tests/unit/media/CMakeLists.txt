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
