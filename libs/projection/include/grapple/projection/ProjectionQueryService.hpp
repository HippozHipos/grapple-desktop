#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectQuery.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/TimelineProjector.hpp>

namespace grapple::projection {

class ProjectionQueryService {
public:
  explicit ProjectionQueryService(const project::IProjectQueryService& projectQueries);

  foundation::Result<BuildTimelineIRResult> buildCurrentTimelineIR() const;
  foundation::Result<BuildRenderPlanResult> buildCurrentRenderPlan() const;

private:
  const project::IProjectQueryService& projectQueries_;
  TimelineProjector timelineProjector_;
  RenderPlanBuilder renderPlanBuilder_;
};

} // namespace grapple::projection
