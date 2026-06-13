#include <grapple/runtime/RuntimeDependencyPlanner.hpp>

#include <grapple/projection/RenderPlanHashes.hpp>
#include <grapple/runtime/RuntimeCache.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace grapple::runtime {

namespace {

struct RenderNodeDependency {
  foundation::NodeId nodeId;
  RuntimeDependencyId dependencyId;
};

RuntimeDependencyId dependencyIdFor(foundation::NodeId nodeId) {
  return runtimeDependencyIdForNode(nodeId);
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

std::optional<RuntimeAssetDependency> findAssetDependency(
  const projection::RenderPlan& plan,
  foundation::AssetId assetId
) {
  for (const projection::RenderAsset& asset : plan.assets) {
    if (asset.assetId == assetId) {
      return RuntimeAssetDependency{asset.assetId, asset.versionHash};
    }
  }

  return std::nullopt;
}

std::vector<RuntimeAssetDependency> assetDependenciesForClip(
  const projection::RenderPlan& plan,
  const projection::RenderClip& clip
) {
  std::vector<RuntimeAssetDependency> dependencies;
  const std::optional<RuntimeAssetDependency> assetDependency = findAssetDependency(
    plan,
    clip.payload.assetId
  );
  if (assetDependency.has_value()) {
    dependencies.push_back(assetDependency.value());
  }
  return dependencies;
}

std::vector<RuntimeAssetDependency> assetDependenciesForEffect(
  const projection::RenderPlan& plan,
  const projection::RenderEffectNode& effectNode
) {
  std::vector<RuntimeAssetDependency> dependencies;
  if (effectNode.payload.implementation.source.sourceAssetId.has_value()) {
    const std::optional<RuntimeAssetDependency> assetDependency = findAssetDependency(
      plan,
      effectNode.payload.implementation.source.sourceAssetId.value()
    );
    if (assetDependency.has_value()) {
      dependencies.push_back(assetDependency.value());
    }
  }
  return dependencies;
}

std::vector<RuntimeModelDependency> modelDependenciesForEffect(
  const projection::RenderEffectNode& effectNode
) {
  std::vector<RuntimeModelDependency> dependencies;
  dependencies.reserve(effectNode.payload.modelDependencies.size());
  for (const timeline::EffectModelDependency& modelDependency : effectNode.payload.modelDependencies) {
    dependencies.push_back(RuntimeModelDependency{
      modelDependency.modelId,
      modelDependency.versionHash
    });
  }
  std::sort(
    dependencies.begin(),
    dependencies.end(),
    [](const RuntimeModelDependency& left, const RuntimeModelDependency& right) {
      return left.modelId < right.modelId;
    }
  );
  return dependencies;
}

RuntimeDependencyNode* findDependencyNode(
  std::vector<RuntimeDependencyNode>& nodes,
  RuntimeDependencyId dependencyId
) {
  for (RuntimeDependencyNode& node : nodes) {
    if (node.id == dependencyId) {
      return &node;
    }
  }

  return nullptr;
}

const RuntimeDependencyNode* findDependencyNode(
  const std::vector<RuntimeDependencyNode>& nodes,
  RuntimeDependencyId dependencyId
) {
  for (const RuntimeDependencyNode& node : nodes) {
    if (node.id == dependencyId) {
      return &node;
    }
  }

  return nullptr;
}

bool containsDependency(
  const std::vector<RuntimeDependencyId>& dependencies,
  RuntimeDependencyId dependencyId
) {
  for (const RuntimeDependencyId& dependency : dependencies) {
    if (dependency == dependencyId) {
      return true;
    }
  }

  return false;
}

void addInputDependency(
  RuntimeDependencyNode& node,
  RuntimeDependencyId dependencyId
) {
  if (!containsDependency(node.inputDependencies, dependencyId)) {
    node.inputDependencies.push_back(dependencyId);
  }
}

bool dependencyNodeChanged(
  const RuntimeDependencyNode& previous,
  const RuntimeDependencyNode& next
) {
  return previous.renderNodeId != next.renderNodeId ||
         !(previous.implementationHash == next.implementationHash) ||
         !(previous.paramsHash == next.paramsHash) ||
         previous.assetDependencies != next.assetDependencies ||
         previous.modelDependencies != next.modelDependencies ||
         previous.inputDependencies != next.inputDependencies ||
         previous.activeRange != next.activeRange;
}

} // namespace

RuntimeDependencyId runtimeDependencyIdForNode(foundation::NodeId nodeId) {
  return RuntimeDependencyId{"dep_" + nodeId.value()};
}

RuntimeDependencyGraph RuntimeDependencyPlanner::build(const projection::RenderPlan& plan) const {
  RuntimeDependencyGraph graph{
    plan.projectId,
    projection::hashRenderPlan(plan),
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
      assetDependenciesForClip(plan, clip),
      {},
      {},
      clip.payload.timelineRange
    });
    dependencies.push_back(RenderNodeDependency{clip.sourceNodeId, dependencyId});
  }

  for (const projection::RenderCamera& camera : plan.cameras) {
    const RuntimeDependencyId dependencyId = dependencyIdFor(camera.sourceNodeId);
    graph.nodes.push_back(RuntimeDependencyNode{
      dependencyId,
      camera.sourceNodeId,
      projection::hashRenderCameraImplementation(),
      projection::hashRenderCameraParams(camera),
      {},
      {},
      {},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, plan.duration}
    });
    dependencies.push_back(RenderNodeDependency{camera.sourceNodeId, dependencyId});
  }

  for (const projection::RenderEffectGraph& effectGraph : plan.effectGraphs) {
    for (const projection::RenderEffectNode& effectNode : effectGraph.nodes) {
      const RuntimeDependencyId dependencyId = dependencyIdFor(effectNode.sourceNodeId);
      graph.nodes.push_back(RuntimeDependencyNode{
        dependencyId,
        effectNode.sourceNodeId,
        projection::hashRenderEffectImplementation(effectNode),
        projection::hashRenderEffectParams(effectNode),
        assetDependenciesForEffect(plan, effectNode),
        modelDependenciesForEffect(effectNode),
        {},
        effectNode.payload.activeRange
      });
      dependencies.push_back(RenderNodeDependency{effectNode.sourceNodeId, dependencyId});
    }

    for (const projection::RenderEffectNode& effectNode : effectGraph.nodes) {
      RuntimeDependencyNode* dependencyNode = findDependencyNode(
        graph.nodes,
        dependencyIdFor(effectNode.sourceNodeId)
      );
      if (dependencyNode == nullptr) {
        continue;
      }

      const std::optional<RuntimeDependencyId> targetDependency = findDependencyForNode(
        dependencies,
        effectGraph.targetNodeId
      );
      if (targetDependency.has_value()) {
        addInputDependency(*dependencyNode, targetDependency.value());
      }

      for (const projection::RenderEffectEdge& edge : effectGraph.edges) {
        if (edge.targetNodeId != effectNode.sourceNodeId) {
          continue;
        }

        const std::optional<RuntimeDependencyId> sourceDependency = findDependencyForNode(
          dependencies,
          edge.sourceNodeId
        );
        if (sourceDependency.has_value()) {
          addInputDependency(*dependencyNode, sourceDependency.value());
        }
      }
    }
  }

  return graph;
}

RuntimeInvalidationResult RuntimeDependencyPlanner::diff(const RuntimeInvalidationRequest& request) const {
  RuntimeDependencyGraph nextGraph = build(request.nextPlan);
  std::vector<RuntimeDependencyId> invalidatedDependencies;
  std::vector<RuntimeCacheKey> invalidatedCacheKeys;

  for (const RuntimeDependencyNode& nextNode : nextGraph.nodes) {
    const RuntimeDependencyNode* previousNode = findDependencyNode(
      request.previousGraph.nodes,
      nextNode.id
    );

    if (previousNode == nullptr || dependencyNodeChanged(*previousNode, nextNode)) {
      invalidatedDependencies.push_back(nextNode.id);
    }
  }

  for (const RuntimeDependencyNode& previousNode : request.previousGraph.nodes) {
    if (findDependencyNode(nextGraph.nodes, previousNode.id) == nullptr &&
        !containsDependency(invalidatedDependencies, previousNode.id)) {
      invalidatedDependencies.push_back(previousNode.id);
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (const RuntimeDependencyNode& nextNode : nextGraph.nodes) {
      if (containsDependency(invalidatedDependencies, nextNode.id)) {
        continue;
      }

      for (const RuntimeDependencyId& inputDependency : nextNode.inputDependencies) {
        if (containsDependency(invalidatedDependencies, inputDependency)) {
          invalidatedDependencies.push_back(nextNode.id);
          changed = true;
          break;
        }
      }
    }
  }

  for (RuntimeDependencyId dependencyId : invalidatedDependencies) {
    const RuntimeDependencyNode* previousNode = findDependencyNode(
      request.previousGraph.nodes,
      dependencyId
    );
    if (previousNode != nullptr) {
      invalidatedCacheKeys.push_back(runtimeCacheKeyForDependency(
        request.previousGraph,
        *previousNode,
        request.runtimeVersion
      ));
    }
  }

  return RuntimeInvalidationResult{
    std::move(nextGraph),
    std::move(invalidatedDependencies),
    std::move(invalidatedCacheKeys)
  };
}

} // namespace grapple::runtime
