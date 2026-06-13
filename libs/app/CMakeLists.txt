find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

add_library(grapple_app
  src/NativeEffectSession.cpp
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
    Grapple::Agent
    Grapple::Foundation
    Grapple::Media
    Grapple::Project
    Grapple::Projection
    Grapple::Render
    Grapple::Runtime
    Grapple::Storage
  PRIVATE
    ${OpenCV_LIBS}
)

target_include_directories(grapple_app
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)
