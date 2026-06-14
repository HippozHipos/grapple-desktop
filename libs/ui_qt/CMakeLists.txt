find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_library(grapple_ui_qt
  src/CameraPropertyPanel.cpp
  src/ClipTransformPanel.cpp
  src/CompositionViewport.cpp
  src/EffectParamPanel.cpp
  src/ExportSettingsPanel.cpp
  src/PreviewSurface.cpp
  src/StewardPanel.cpp
  src/TimelinePanel.cpp
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
