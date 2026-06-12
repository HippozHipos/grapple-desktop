find_package(Qt5 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED COMPONENTS core videoio)

add_executable(grapple_desktop
  main.cpp
)

target_link_libraries(grapple_desktop
  PRIVATE
    Grapple::App
    Grapple::DemoProject
    Grapple::Media
    Qt5::Widgets
    ${OpenCV_LIBS}
)

target_include_directories(grapple_desktop
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)
