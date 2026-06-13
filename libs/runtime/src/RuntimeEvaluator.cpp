#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <grapple/runtime/RuntimeParamEvaluator.hpp>
#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace grapple::runtime {

namespace {

bool containsDependency(
  const std::vector<RuntimeDependencyId>& dependencies,
  RuntimeDependencyId dependencyId
) {
  for (const RuntimeDependencyId& dependency : dependencies) {
    if (dependency == dependencyId) {
      return true;
    }
  }

  return false;
}

const PreparedEffectNode* findPreparedEffect(
  const PreparedRuntimePlan& prepared,
  foundation::NodeId sourceNodeId
) {
  for (const PreparedEffectNode& effect : prepared.preparedEffects) {
    if (effect.sourceNodeId == sourceNodeId) {
      return &effect;
    }
  }

  return nullptr;
}

} // namespace

RuntimeEvaluator::RuntimeEvaluator() = default;

RuntimeEvaluator::RuntimeEvaluator(std::vector<IEffectRuntime*> effectRuntimes)
  : effectRuntimes_{std::move(effectRuntimes)} {}

foundation::Result<PrepareRuntimePlanResult> RuntimeEvaluator::prepare(
  const PrepareRuntimePlanRequest& request
) const {
  const RuntimeDependencyPlanner planner;
  RuntimeDependencyGraph graph;
  std::vector<RuntimeDependencyId> invalidatedDependencies;
  const bool canReusePreviousPrepared =
    request.previousPrepared != nullptr &&
    request.previousPrepared->diagnostics.empty() &&
    request.previousPrepared->dependencyGraph.projectId == request.plan.projectId;

  if (canReusePreviousPrepared) {
    RuntimeInvalidationResult invalidation = planner.diff(RuntimeInvalidationRequest{
      request.previousPrepared->dependencyGraph,
      request.plan,
      request.runtimeVersion
    });
    if (request.cache != nullptr && !invalidation.invalidatedCacheKeys.empty()) {
      auto invalidated = request.cache->invalidate(invalidation.invalidatedCacheKeys);
      if (!invalidated) {
        return invalidated.error();
      }
    }
    graph = std::move(invalidation.nextGraph);
    invalidatedDependencies = std::move(invalidation.invalidatedDependencies);
  } else {
    graph = planner.build(request.plan);
  }

  PreparedRuntimePlan prepared{
    request.plan.revision,
    graph.planHash,
    graph,
    request.plan.layers,
    request.plan.clips,
    request.plan.audioClips,
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

      const RuntimeDependencyId dependencyId = runtimeDependencyIdForNode(effectNode.sourceNodeId);
      if (canReusePreviousPrepared && !containsDependency(invalidatedDependencies, dependencyId)) {
        const PreparedEffectNode* previousEffect = findPreparedEffect(
          *request.previousPrepared,
          effectNode.sourceNodeId
        );
        if (previousEffect != nullptr && previousEffect->runtime == selectedRuntime) {
          prepared.preparedEffects.push_back(*previousEffect);
          continue;
        }
      }

      auto effectPrepare = selectedRuntime->prepare(EffectPrepareRequest{
        request.plan.projectId,
        request.plan.revision,
        effectGraph,
        effectNode
      });
      if (!effectPrepare) {
        return effectPrepare.error();
      }

      PreparedEffectNode preparedEffect = std::move(effectPrepare.value().prepared);
      preparedEffect.runtime = selectedRuntime;
      preparedEffect.params = runtimeParamsFromEffectNode(effectNode);
      prepared.preparedEffects.push_back(std::move(preparedEffect));
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

  for (const projection::RenderAudioClip& clip : request.prepared.audioClips) {
    if (clip.payload.timelineRange.contains(request.time)) {
      sample.audioClips.push_back(ResolvedAudioClip{clip.sourceNodeId, clip.trackNodeId, clip.payload});
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

  for (const PreparedEffectNode& effect : request.prepared.preparedEffects) {
    if (effect.runtime == nullptr) {
      sample.diagnostics.push_back(RuntimeDiagnostic{
        "runtime.effect_runtime_missing",
        DiagnosticSeverity::Error,
        DiagnosticLocation{
          request.prepared.dependencyGraph.projectId,
          request.prepared.sourceRevision,
          effect.sourceNodeId
        },
        "Prepared effect node " + effect.sourceNodeId.value() + " has no runtime."
      });
      continue;
    }

    auto effectProcess = effect.runtime->process(EffectProcessRequest{
      effect,
      request.time,
      request.quality,
      evaluateRuntimeParams(effect.params, request.time)
    });
    if (!effectProcess) {
      return effectProcess.error();
    }

    sample.effectOutputs.push_back(std::move(effectProcess.value().output));
    sample.diagnostics.insert(
      sample.diagnostics.end(),
      effectProcess.value().diagnostics.begin(),
      effectProcess.value().diagnostics.end()
    );
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
