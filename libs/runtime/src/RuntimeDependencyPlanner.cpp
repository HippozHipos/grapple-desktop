#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

#include <grapple/projection/RenderPlanHashes.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>

namespace grapple::runtime {

namespace {

RuntimeDependencyId dependencyIdFor(foundation::NodeId nodeId) {
  return RuntimeDependencyId{"dep_" + nodeId.value()};
}

} // namespace

RuntimeDependencyGraph RuntimeDependencyPlanner::build(const projection::RenderPlan& plan) const {
  RuntimeDependencyGraph graph{
    foundation::stableHash(projection::serializeCanonicalRenderPlan(plan)),
    {}
  };

  for (const projection::RenderClip& clip : plan.clips) {
    graph.nodes.push_back(RuntimeDependencyNode{
      dependencyIdFor(clip.sourceNodeId),
      clip.sourceNodeId,
      projection::hashRenderClipImplementation(),
      projection::hashRenderClipParams(clip),
      {},
      clip.payload.timelineRange
    });
  }

  for (const projection::RenderEffectGraph& effectGraph : plan.effectGraphs) {
    for (const projection::RenderEffectNode& effectNode : effectGraph.nodes) {
      graph.nodes.push_back(RuntimeDependencyNode{
        dependencyIdFor(effectNode.sourceNodeId),
        effectNode.sourceNodeId,
        projection::hashRenderEffectImplementation(effectNode),
        projection::hashRenderEffectParams(effectNode),
        {},
        effectNode.payload.activeRange
      });
    }
  }

  return graph;
}

} // namespace grapple::runtime
