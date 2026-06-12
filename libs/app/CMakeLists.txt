add_library(grapple_app
  src/NativeExportSession.cpp
  src/NativeMediaFrameSource.cpp
  src/NativeProjectCommandWriter.cpp
  src/NativePreviewSession.cpp
  src/NativeProjectSession.cpp
  src/NativeStewardSession.cpp
  src/NativeWorkspaceSession.cpp
)

add_library(Grapple::App ALIAS grapple_app)

target_include_directories(grapple_app
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_app
  PUBLIC
    Grapple::Foundation
    Grapple::Media
    Grapple::Project
    Grapple::Projection
    Grapple::Render
    Grapple::Runtime
    Grapple::Storage
)
