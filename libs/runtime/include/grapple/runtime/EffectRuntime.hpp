#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/runtime/RuntimeOutput.hpp>
#include <grapple/runtime/RuntimeQuality.hpp>

#include <vector>

namespace grapple::runtime {

class IEffectRuntime;

struct PreparedEffectNode {
  foundation::GraphId effectGraphId;
  foundation::NodeId targetNodeId;
  foundation::NodeId sourceNodeId;
  IEffectRuntime* runtime = nullptr;
  RuntimeValueMap preparedValues;
};

struct EffectPrepareRequest {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  const projection::RenderEffectGraph& graph;
  const projection::RenderEffectNode& node;
};

struct EffectPrepareResult {
  PreparedEffectNode prepared;
  std::vector<RuntimeDiagnostic> diagnostics;
};

struct EffectProcessRequest {
  const PreparedEffectNode& prepared;
  foundation::TimeSeconds time;
  RuntimeQuality quality = RuntimeQuality::Interactive;
};

struct EffectProcessResult {
  RuntimeEffectOutput output;
  std::vector<RuntimeDiagnostic> diagnostics;
};

class IEffectRuntime {
public:
  virtual ~IEffectRuntime() = default;

  [[nodiscard]] virtual bool supports(const projection::RenderEffectNode& node) const = 0;
  virtual foundation::Result<EffectPrepareResult> prepare(const EffectPrepareRequest& request) = 0;
  virtual foundation::Result<EffectProcessResult> process(const EffectProcessRequest& request) = 0;
};

} // namespace grapple::runtime
