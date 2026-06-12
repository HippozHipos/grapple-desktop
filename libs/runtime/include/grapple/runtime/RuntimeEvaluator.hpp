#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeDependencyGraph.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/runtime/RuntimeSample.hpp>

#include <vector>

namespace grapple::runtime {

enum class RuntimePrepareMode {
  Interactive,
  Export
};

struct PreparedRuntimePlan {
  foundation::RevisionId sourceRevision;
  foundation::Hash256 planHash;
  RuntimeDependencyGraph dependencyGraph;
  std::vector<projection::RenderLayer> layers;
  std::vector<projection::RenderClip> clips;
  std::vector<projection::RenderCamera> cameras;
  std::vector<RuntimeDiagnostic> diagnostics;
};

struct PrepareRuntimePlanRequest {
  projection::RenderPlan plan;
  RuntimePrepareMode mode = RuntimePrepareMode::Interactive;
};

struct PrepareRuntimePlanResult {
  PreparedRuntimePlan prepared;
  std::vector<RuntimeDiagnostic> diagnostics;
};

class RuntimeEvaluator {
public:
  foundation::Result<PrepareRuntimePlanResult> prepare(
    const PrepareRuntimePlanRequest& request
  ) const;
  foundation::Result<RuntimeSampleResult> sample(
    const RuntimeSampleRequest& request
  ) const;
};

} // namespace grapple::runtime
