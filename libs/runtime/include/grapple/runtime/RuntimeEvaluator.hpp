#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/EffectRuntime.hpp>
#include <grapple/runtime/RuntimeCache.hpp>
#include <grapple/runtime/RuntimeDependencyGraph.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/runtime/RuntimeSample.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

struct PreparedRuntimePlan {
  foundation::RevisionId sourceRevision;
  foundation::Hash256 planHash;
  RuntimeDependencyGraph dependencyGraph;
  std::vector<projection::RenderLayer> layers;
  std::vector<projection::RenderClip> clips;
  std::vector<projection::RenderTextClip> textClips;
  std::vector<projection::RenderAudioClip> audioClips;
  std::vector<projection::RenderCamera> cameras;
  std::vector<PreparedEffectNode> preparedEffects;
  std::vector<RuntimeDiagnostic> diagnostics;
};

struct PrepareRuntimePlanRequest {
  projection::RenderPlan plan;
  const PreparedRuntimePlan* previousPrepared = nullptr;
  IRuntimeCache* cache = nullptr;
  std::string runtimeVersion = "runtime_prepare_v1";
};

struct PrepareRuntimePlanResult {
  PreparedRuntimePlan prepared;
  std::vector<RuntimeDiagnostic> diagnostics;
};

class RuntimeEvaluator {
public:
  RuntimeEvaluator();
  explicit RuntimeEvaluator(std::vector<IEffectRuntime*> effectRuntimes);

  foundation::Result<PrepareRuntimePlanResult> prepare(
    const PrepareRuntimePlanRequest& request
  ) const;
  foundation::Result<RuntimeSampleResult> sample(
    const RuntimeSampleRequest& request
  ) const;
  foundation::Result<RuntimeRangeResult> evaluateRange(
    const RuntimeRangeRequest& request
  ) const;

private:
  std::vector<IEffectRuntime*> effectRuntimes_;
};

} // namespace grapple::runtime
