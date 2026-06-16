find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs videoio)

add_executable(grapple_desktop
  src/DesktopApp.cpp
  src/DesktopWindow.cpp
  src/main.cpp
)

set_target_properties(grapple_desktop PROPERTIES
  OUTPUT_NAME grapple
)

add_custom_command(TARGET grapple_desktop POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:grapple_desktop>/grapple_desktop"
)

target_compile_definitions(grapple_desktop
  PRIVATE
    GRAPPLE_NATIVE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(grapple_desktop
  PRIVATE
    Grapple::App
    Grapple::DemoProject
    Grapple::Media
    Grapple::UiQt
    Qt5::Widgets
    ${OpenCV_LIBS}
)

target_include_directories(grapple_desktop
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

install(TARGETS grapple_desktop
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
