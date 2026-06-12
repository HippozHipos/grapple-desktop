find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(grapple_desktop
  main.cpp
)

target_link_libraries(grapple_desktop
  PRIVATE
    Grapple::App
    Grapple::DemoProject
    Qt5::Widgets
)
