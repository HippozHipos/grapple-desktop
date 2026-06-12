#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

using RuntimeDependencyId = foundation::StrongId<struct RuntimeDependencyIdTag>;

struct RuntimeAssetDependency {
  foundation::AssetId assetId;
  foundation::Hash256 versionHash;

  friend bool operator==(const RuntimeAssetDependency&, const RuntimeAssetDependency&) = default;
};

struct RuntimeDependencyNode {
  RuntimeDependencyId id;
  foundation::NodeId renderNodeId;
  foundation::Hash256 implementationHash;
  foundation::Hash256 paramsHash;
  std::vector<RuntimeAssetDependency> assetDependencies;
  std::vector<RuntimeDependencyId> inputDependencies;
  foundation::TimeRange activeRange;
};

struct RuntimeDependencyGraph {
  foundation::ProjectId projectId;
  foundation::Hash256 planHash;
  std::vector<RuntimeDependencyNode> nodes;
};

} // namespace grapple::runtime
