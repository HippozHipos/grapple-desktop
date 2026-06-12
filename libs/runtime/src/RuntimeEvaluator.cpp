#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace grapple::runtime {

RuntimeEvaluator::RuntimeEvaluator() = default;

RuntimeEvaluator::RuntimeEvaluator(std::vector<IEffectRuntime*> effectRuntimes)
  : effectRuntimes_{std::move(effectRuntimes)} {}

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
    {},
    {}
  };

  for (const projection::RenderEffectGraph& effectGraph : request.plan.effectGraphs) {
    for (const projection::RenderEffectNode& effectNode : effectGraph.nodes) {
      IEffectRuntime* selectedRuntime = nullptr;
      for (IEffectRuntime* runtime : effectRuntimes_) {
        if (runtime->supports(effectNode)) {
          selectedRuntime = runtime;
          break;
        }
      }

      if (selectedRuntime == nullptr) {
        prepared.diagnostics.push_back(RuntimeDiagnostic{
          "runtime.effect_runtime_missing",
          DiagnosticSeverity::Error,
          DiagnosticLocation{
            request.plan.projectId,
            request.plan.revision,
            effectNode.sourceNodeId
          },
          "No runtime supports effect node " + effectNode.sourceNodeId.value() + "."
        });
        continue;
      }

      auto effectPrepare = selectedRuntime->prepare(EffectPrepareRequest{
        effectGraph,
        effectNode
      });
      if (!effectPrepare) {
        return effectPrepare.error();
      }

      prepared.preparedEffects.push_back(std::move(effectPrepare.value().prepared));
      prepared.diagnostics.insert(
        prepared.diagnostics.end(),
        effectPrepare.value().diagnostics.begin(),
        effectPrepare.value().diagnostics.end()
      );
    }
  }

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
    sample.layers.push_back(ResolvedLayer{layer.sourceNodeId, layer.name});
  }

  for (const projection::RenderClip& clip : request.prepared.clips) {
    if (clip.payload.timelineRange.contains(request.time)) {
      sample.clips.push_back(ResolvedClip{clip.sourceNodeId, clip.trackNodeId, clip.payload});
    }
  }

  for (const projection::RenderCamera& camera : request.prepared.cameras) {
    sample.cameras.push_back(ResolvedCamera{
      camera.sourceNodeId,
      camera.name,
      camera.transform,
      camera.lens
    });
  }

  return RuntimeSampleResult{sample, sample.diagnostics};
}

foundation::Result<RuntimeRangeResult> RuntimeEvaluator::evaluateRange(
  const RuntimeRangeRequest& request
) const {
  RuntimeRangeResult result{
    request.range,
    {},
    request.prepared.diagnostics
  };

  const double framesPerSecond = request.frameRate.framesPerSecond();
  const double duration = request.range.duration();
  const auto frameCount = static_cast<std::int64_t>(duration * framesPerSecond);
  result.frames.reserve(static_cast<std::size_t>(frameCount));

  for (std::int64_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    const foundation::TimeSeconds time{
      request.range.start.value + static_cast<double>(frameIndex) / framesPerSecond
    };
    auto frameSample = sample(RuntimeSampleRequest{
      request.prepared,
      time,
      request.quality
    });
    if (!frameSample) {
      return frameSample.error();
    }

    result.frames.push_back(RuntimeFrameResult{
      foundation::FrameNumber{frameIndex},
      std::move(frameSample.value().sample)
    });
  }

  return result;
}

} // namespace grapple::runtime
