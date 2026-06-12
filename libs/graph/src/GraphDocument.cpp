#include <grapple/graph/GraphDocument.hpp>

#include <algorithm>

namespace grapple::graph {

const std::vector<GraphNode>& GraphDocument::nodes() const noexcept {
  return nodes_;
}

const std::vector<GraphEdge>& GraphDocument::edges() const noexcept {
  return edges_;
}

const GraphNode* GraphDocument::findNode(foundation::NodeId id) const noexcept {
  const auto iterator = std::find_if(nodes_.begin(), nodes_.end(), [&](const GraphNode& node) {
    return node.id == id;
  });

  if (iterator == nodes_.end()) {
    return nullptr;
  }

  return &*iterator;
}

bool GraphDocument::hasNode(foundation::NodeId id) const noexcept {
  return findNode(id) != nullptr;
}

foundation::Result<void> GraphDocument::addNode(GraphNode node) {
  if (!node.id) {
    return foundation::Error{"graph.node_id_empty", "Graph node id must not be empty."};
  }

  if (hasNode(node.id)) {
    return foundation::Error{"graph.node_id_duplicate", "Graph node id already exists."};
  }

  nodes_.push_back(std::move(node));
  return {};
}

foundation::Result<void> GraphDocument::addEdge(GraphEdge edge) {
  if (!edge.id) {
    return foundation::Error{"graph.edge_id_empty", "Graph edge id must not be empty."};
  }

  const auto edgeExists = std::any_of(edges_.begin(), edges_.end(), [&](const GraphEdge& existing) {
    return existing.id == edge.id;
  });
  if (edgeExists) {
    return foundation::Error{"graph.edge_id_duplicate", "Graph edge id already exists."};
  }

  if (!hasNode(edge.sourceNodeId) || !hasNode(edge.targetNodeId)) {
    return foundation::Error{"graph.edge_node_missing", "Graph edge endpoints must exist."};
  }

  edges_.push_back(std::move(edge));
  return {};
}

} // namespace grapple::graph

