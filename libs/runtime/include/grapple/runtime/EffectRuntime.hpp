#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/runtime/RuntimeValue.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

struct RuntimeNamedValue {
  std::string name;
  RuntimeValue value;
};

using RuntimeValueMap = std::vector<RuntimeNamedValue>;

struct PreparedEffectNode {
  foundation::GraphId effectGraphId;
  foundation::NodeId targetNodeId;
  foundation::NodeId sourceNodeId;
  RuntimeValueMap preparedValues;
};

struct EffectPrepareRequest {
  const projection::RenderEffectGraph& graph;
  const projection::RenderEffectNode& node;
};

struct EffectPrepareResult {
  PreparedEffectNode prepared;
  std::vector<RuntimeDiagnostic> diagnostics;
};

class IEffectRuntime {
public:
  virtual ~IEffectRuntime() = default;

  [[nodiscard]] virtual bool supports(const projection::RenderEffectNode& node) const = 0;
  virtual foundation::Result<EffectPrepareResult> prepare(const EffectPrepareRequest& request) = 0;
};

} // namespace grapple::runtime
