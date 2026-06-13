find_package(OpenCV REQUIRED COMPONENTS core videoio)

add_library(grapple_demo_project
  DemoProject.cpp
)

add_library(Grapple::DemoProject ALIAS grapple_demo_project)

target_include_directories(grapple_demo_project
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(grapple_demo_project
  PUBLIC
    Grapple::App
  PRIVATE
    ${OpenCV_LIBS}
)

target_include_directories(grapple_demo_project
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)
