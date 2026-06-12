#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/projection/TimelineIR.hpp>

namespace grapple::projection {

struct BuildRenderPlanRequest {
  TimelineIR timeline;
};

struct BuildRenderPlanResult {
  RenderPlan plan;
  std::vector<ProjectionDiagnostic> diagnostics;
};

class RenderPlanBuilder {
public:
  foundation::Result<BuildRenderPlanResult> buildRenderPlan(
    const BuildRenderPlanRequest& request
  ) const;
};

} // namespace grapple::projection

