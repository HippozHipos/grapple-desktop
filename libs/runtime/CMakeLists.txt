add_library(grapple_runtime
  src/RuntimeDependencyPlanner.cpp
  src/RuntimeEvaluator.cpp
)

add_library(Grapple::Runtime ALIAS grapple_runtime)

target_include_directories(grapple_runtime
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_runtime
  PUBLIC
    Grapple::Foundation
    Grapple::Projection
)

