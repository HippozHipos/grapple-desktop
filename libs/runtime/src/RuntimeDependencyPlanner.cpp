#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

#include <grapple/projection/RenderPlanSerializer.hpp>

namespace grapple::runtime {

RuntimeDependencyGraph RuntimeDependencyPlanner::build(const projection::RenderPlan& plan) const {
  return RuntimeDependencyGraph{
    foundation::stableHash(projection::serializeCanonicalRenderPlan(plan)),
    {}
  };
}

} // namespace grapple::runtime
