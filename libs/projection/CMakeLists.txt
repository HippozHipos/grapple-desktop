add_library(grapple_projection
  src/TimelineProjector.cpp
  src/ProjectionQueryService.cpp
  src/RenderPlanBuilder.cpp
  src/RenderPlanHashes.cpp
  src/RenderPlanSerializer.cpp
)

add_library(Grapple::Projection ALIAS grapple_projection)

target_include_directories(grapple_projection
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_projection
  PUBLIC
    Grapple::Foundation
    Grapple::Asset
    Grapple::Graph
    Grapple::Project
    Grapple::Timeline
)
