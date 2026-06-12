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
    request.plan.layers,
    request.plan.clips,
    request.plan.cameras,
    {}
  };

  return PrepareRuntimePlanResult{prepared, prepared.diagnostics};
}

foundation::Result<RuntimeSampleResult> RuntimeEvaluator::sample(
  const RuntimeSampleRequest& request
) const {
  RuntimeSample sample{
    request.time,
    {},
    {},
    {},
    request.prepared.diagnostics
  };

  for (const projection::RenderLayer& layer : request.prepared.layers) {
    if (layer.enabled) {
      sample.layers.push_back(ResolvedLayer{layer.sourceNodeId, layer.name, layer.enabled});
    }
  }

  for (const projection::RenderClip& clip : request.prepared.clips) {
    if (clip.enabled && clip.payload.timelineRange.contains(request.time)) {
      sample.clips.push_back(ResolvedClip{clip.sourceNodeId, clip.trackNodeId, clip.payload, clip.enabled});
    }
  }

  for (const projection::RenderCamera& camera : request.prepared.cameras) {
    if (camera.enabled) {
      sample.cameras.push_back(ResolvedCamera{
        camera.sourceNodeId,
        camera.name,
        camera.transform,
        camera.lens,
        camera.enabled
      });
    }
  }

  return RuntimeSampleResult{sample, sample.diagnostics};
}

} // namespace grapple::runtime
