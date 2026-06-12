#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/graph/GraphDocument.hpp>

#include <vector>

namespace grapple::graph {

struct GraphDiff {
  std::vector<foundation::NodeId> addedNodes;
  std::vector<foundation::NodeId> removedNodes;
  std::vector<foundation::NodeId> changedNodes;
  std::vector<foundation::EdgeId> addedEdges;
  std::vector<foundation::EdgeId> removedEdges;
  std::vector<foundation::EdgeId> changedEdges;
};

GraphDiff diffGraphs(const GraphDocument& before, const GraphDocument& after);

} // namespace grapple::graph

