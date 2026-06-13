#pragma once

#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeCache.hpp>
#include <grapple/runtime/RuntimeDependencyGraph.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

struct RuntimeInvalidationRequest {
  const RuntimeDependencyGraph& previousGraph;
  const projection::RenderPlan& nextPlan;
  std::string runtimeVersion;
};

struct RuntimeInvalidationResult {
  RuntimeDependencyGraph nextGraph;
  std::vector<RuntimeDependencyId> invalidatedDependencies;
  std::vector<RuntimeCacheKey> invalidatedCacheKeys;
};

RuntimeDependencyId runtimeDependencyIdForNode(foundation::NodeId nodeId);

class RuntimeDependencyPlanner {
public:
  RuntimeDependencyGraph build(const projection::RenderPlan& plan) const;
  RuntimeInvalidationResult diff(const RuntimeInvalidationRequest& request) const;
};

} // namespace grapple::runtime
