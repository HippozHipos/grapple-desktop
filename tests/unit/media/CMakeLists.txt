add_executable(grapple_media_tests
  FrameCacheTest.cpp
)

target_link_libraries(grapple_media_tests
  PRIVATE
    Grapple::Media
)

target_include_directories(grapple_media_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_media_tests COMMAND grapple_media_tests)

