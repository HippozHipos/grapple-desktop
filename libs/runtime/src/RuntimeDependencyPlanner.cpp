#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

#include <grapple/projection/RenderPlanHashes.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>

#include <optional>
#include <utility>

namespace grapple::runtime {

namespace {

struct RenderNodeDependency {
  foundation::NodeId nodeId;
  RuntimeDependencyId dependencyId;
};

RuntimeDependencyId dependencyIdFor(foundation::NodeId nodeId) {
  return RuntimeDependencyId{"dep_" + nodeId.value()};
}

std::optional<RuntimeDependencyId> findDependencyForNode(
  const std::vector<RenderNodeDependency>& dependencies,
  foundation::NodeId nodeId
) {
  for (const RenderNodeDependency& dependency : dependencies) {
    if (dependency.nodeId == nodeId) {
      return dependency.dependencyId;
    }
  }

  return std::nullopt;
}

} // namespace

RuntimeDependencyGraph RuntimeDependencyPlanner::build(const projection::RenderPlan& plan) const {
  RuntimeDependencyGraph graph{
    foundation::stableHash(projection::serializeCanonicalRenderPlan(plan)),
    {}
  };

  std::vector<RenderNodeDependency> dependencies;

  for (const projection::RenderClip& clip : plan.clips) {
    const RuntimeDependencyId dependencyId = dependencyIdFor(clip.sourceNodeId);
    graph.nodes.push_back(RuntimeDependencyNode{
      dependencyId,
      clip.sourceNodeId,
      projection::hashRenderClipImplementation(),
      projection::hashRenderClipParams(clip),
      {},
      clip.payload.timelineRange
    });
    dependencies.push_back(RenderNodeDependency{clip.sourceNodeId, dependencyId});
  }

  for (const projection::RenderEffectGraph& effectGraph : plan.effectGraphs) {
    for (const projection::RenderEffectNode& effectNode : effectGraph.nodes) {
      std::vector<RuntimeDependencyId> inputDependencies;
      const std::optional<RuntimeDependencyId> targetDependency = findDependencyForNode(
        dependencies,
        effectGraph.targetNodeId
      );
      if (targetDependency.has_value()) {
        inputDependencies.push_back(targetDependency.value());
      }

      const RuntimeDependencyId dependencyId = dependencyIdFor(effectNode.sourceNodeId);
      graph.nodes.push_back(RuntimeDependencyNode{
        dependencyId,
        effectNode.sourceNodeId,
        projection::hashRenderEffectImplementation(effectNode),
        projection::hashRenderEffectParams(effectNode),
        std::move(inputDependencies),
        effectNode.payload.activeRange
      });
      dependencies.push_back(RenderNodeDependency{effectNode.sourceNodeId, dependencyId});
    }
  }

  return graph;
}

} // namespace grapple::runtime
