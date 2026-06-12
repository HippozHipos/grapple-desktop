add_executable(grapple_timeline_tests
  TimelinePayloadTest.cpp
)

target_link_libraries(grapple_timeline_tests
  PRIVATE
    Grapple::Timeline
)

target_include_directories(grapple_timeline_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_timeline_tests COMMAND grapple_timeline_tests)

