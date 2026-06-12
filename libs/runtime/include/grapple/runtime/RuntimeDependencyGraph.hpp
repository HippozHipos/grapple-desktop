#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>

#include <string>
#include <vector>

namespace grapple::runtime {

using RuntimeDependencyId = foundation::StrongId<struct RuntimeDependencyIdTag>;

struct RuntimeDependencyNode {
  RuntimeDependencyId id;
  foundation::NodeId renderNodeId;
  foundation::Hash256 implementationHash;
  foundation::Hash256 paramsHash;
  std::vector<RuntimeDependencyId> inputDependencies;
  foundation::TimeRange activeRange;
};

struct RuntimeDependencyGraph {
  foundation::Hash256 planHash;
  std::vector<RuntimeDependencyNode> nodes;
};

} // namespace grapple::runtime

