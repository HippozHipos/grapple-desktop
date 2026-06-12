#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

#include <sstream>

namespace grapple::runtime {

namespace {

std::string planSignature(const projection::RenderPlan& plan) {
  std::ostringstream stream;
  stream << plan.projectId.value() << '|'
         << plan.revision.value() << '|'
         << plan.stage.name << '|'
         << plan.duration.value << '|';

  for (const projection::RenderLayer& layer : plan.layers) {
    stream << "layer:" << layer.sourceNodeId.value() << ':' << layer.name << ':'
           << (layer.enabled ? "1" : "0") << '|';
  }

  for (const projection::RenderCamera& camera : plan.cameras) {
    stream << "camera:" << camera.sourceNodeId.value() << ':' << camera.name << ':'
           << (camera.enabled ? "1" : "0") << '|';
  }

  for (const projection::RenderEffectGraph& effectGraph : plan.effectGraphs) {
    stream << "effect_graph:" << effectGraph.id.value() << ':'
           << effectGraph.targetNodeId.value() << '|';
  }

  return stream.str();
}

} // namespace

RuntimeDependencyGraph RuntimeDependencyPlanner::build(const projection::RenderPlan& plan) const {
  return RuntimeDependencyGraph{
    foundation::stableHash(planSignature(plan)),
    {}
  };
}

} // namespace grapple::runtime

