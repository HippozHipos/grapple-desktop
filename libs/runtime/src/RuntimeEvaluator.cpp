#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

namespace grapple::runtime {

foundation::Result<PrepareRuntimePlanResult> RuntimeEvaluator::prepare(
  const PrepareRuntimePlanRequest& request
) const {
  const RuntimeDependencyPlanner planner;
  RuntimeDependencyGraph graph = planner.build(request.plan);

  PreparedRuntimePlan prepared{
    request.plan.revision,
    graph.planHash,
    graph,
    {}
  };

  return PrepareRuntimePlanResult{prepared, prepared.diagnostics};
}

} // namespace grapple::runtime

