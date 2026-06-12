#include <grapple/projection/RenderPlanBuilder.hpp>

namespace grapple::projection {

foundation::Result<BuildRenderPlanResult> RenderPlanBuilder::buildRenderPlan(
  const BuildRenderPlanRequest& request
) const {
  RenderPlan plan{
    request.timeline.projectId,
    request.timeline.revision,
    RenderStage{request.timeline.stage.name},
    request.timeline.duration,
    {},
    {},
    {},
    request.timeline.diagnostics
  };

  for (const TimelineLayer& layer : request.timeline.layers) {
    plan.layers.push_back(RenderLayer{layer.sourceNodeId, layer.name, layer.enabled});
  }

  for (const TimelineCamera& camera : request.timeline.cameras) {
    plan.cameras.push_back(RenderCamera{camera.sourceNodeId, camera.name, camera.enabled});
  }

  return BuildRenderPlanResult{plan, plan.diagnostics};
}

} // namespace grapple::projection

