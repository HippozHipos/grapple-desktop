#pragma once

#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeDependencyGraph.hpp>

#include <vector>

namespace grapple::runtime {

struct RuntimeInvalidationRequest {
  const RuntimeDependencyGraph& previousGraph;
  const projection::RenderPlan& nextPlan;
};

struct RuntimeInvalidationResult {
  RuntimeDependencyGraph nextGraph;
  std::vector<RuntimeDependencyId> invalidatedDependencies;
};

class RuntimeDependencyPlanner {
public:
  RuntimeDependencyGraph build(const projection::RenderPlan& plan) const;
  RuntimeInvalidationResult diff(const RuntimeInvalidationRequest& request) const;
};

} // namespace grapple::runtime
