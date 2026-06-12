#include <grapple/projection/ProjectionQueryService.hpp>

#include <variant>

namespace grapple::projection {

ProjectionQueryService::ProjectionQueryService(const project::IProjectQueryService& projectQueries)
  : projectQueries_(projectQueries) {}

foundation::Result<BuildTimelineIRResult> ProjectionQueryService::buildCurrentTimelineIR() const {
  auto snapshotQuery = projectQueries_.query(project::GetProjectSnapshotQuery{});
  if (!snapshotQuery) {
    return snapshotQuery.error();
  }

  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&snapshotQuery.value());
  if (snapshotResult == nullptr) {
    return foundation::Error{"projection.snapshot_result_missing", "Project snapshot query returned the wrong result type."};
  }

  return timelineProjector_.buildTimelineIR(BuildTimelineIRRequest{snapshotResult->snapshot});
}

foundation::Result<BuildRenderPlanResult> ProjectionQueryService::buildCurrentRenderPlan() const {
  auto timeline = buildCurrentTimelineIR();
  if (!timeline) {
    return timeline.error();
  }

  return renderPlanBuilder_.buildRenderPlan(BuildRenderPlanRequest{timeline.value().timeline});
}

} // namespace grapple::projection
