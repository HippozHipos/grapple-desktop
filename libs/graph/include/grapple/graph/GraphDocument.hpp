#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>

#include <optional>
#include <vector>

namespace grapple::graph {

class GraphDocument {
public:
  [[nodiscard]] const std::vector<GraphNode>& nodes() const noexcept;
  [[nodiscard]] const std::vector<GraphEdge>& edges() const noexcept;

  [[nodiscard]] const GraphNode* findNode(foundation::NodeId id) const noexcept;
  [[nodiscard]] bool hasNode(foundation::NodeId id) const noexcept;

  foundation::Result<void> addNode(GraphNode node);
  foundation::Result<void> replaceNodePayload(foundation::NodeId nodeId, NodePayload payload);
  foundation::Result<void> removeNode(foundation::NodeId nodeId);
  foundation::Result<void> addEdge(GraphEdge edge);
  foundation::Result<void> removeEdge(foundation::EdgeId edgeId);

private:
  std::vector<GraphNode> nodes_;
  std::vector<GraphEdge> edges_;
};

} // namespace grapple::graph
