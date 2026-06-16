find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(grapple_ui_qt_tests
  StewardPanelTest.cpp
)

target_link_libraries(grapple_ui_qt_tests
  PRIVATE
    Grapple::UiQt
    Qt5::Widgets
)

target_include_directories(grapple_ui_qt_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_ui_qt_tests COMMAND grapple_ui_qt_tests)
set_tests_properties(grapple_ui_qt_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
