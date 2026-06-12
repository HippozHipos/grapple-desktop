#pragma once

#include <grapple/projection/RenderPlan.hpp>
#include <grapple/runtime/RuntimeDependencyGraph.hpp>

namespace grapple::runtime {

class RuntimeDependencyPlanner {
public:
  RuntimeDependencyGraph build(const projection::RenderPlan& plan) const;
};

} // namespace grapple::runtime

