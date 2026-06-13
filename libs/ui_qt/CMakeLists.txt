find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_library(grapple_ui_qt
  src/EffectParamPanel.cpp
  src/StewardPanel.cpp
)

add_library(Grapple::UiQt ALIAS grapple_ui_qt)

target_include_directories(grapple_ui_qt
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_ui_qt
  PUBLIC
    Grapple::App
    Qt5::Widgets
)
