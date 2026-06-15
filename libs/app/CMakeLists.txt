find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc videoio)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

add_library(grapple_app
  src/AppViewModel.cpp
  src/NativeEffectSession.cpp
  src/NativeExportSession.cpp
  src/NativeMediaFrameSource.cpp
  src/NativeMediaImport.cpp
  src/NativeProjectCommandWriter.cpp
  src/NativePreviewSession.cpp
  src/NativeProjectSession.cpp
  src/NativeStewardPlanner.cpp
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
    Grapple::Effects
    Grapple::Foundation
    Grapple::Jobs
    Grapple::Media
    Grapple::Project
    Grapple::Projection
    Grapple::Render
    Grapple::Runtime
    Grapple::Storage
  PRIVATE
    ${OpenCV_LIBS}
    ${JSONCPP_LIBRARIES}
)

target_include_directories(grapple_app
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_directories(grapple_app
  PRIVATE
    ${JSONCPP_LIBRARY_DIRS}
)
