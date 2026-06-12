#include <grapple/graph/GraphDocument.hpp>

#include <algorithm>
#include <cstdlib>

namespace grapple::graph {

namespace {

bool payloadMatchesNodeKind(const GraphNode& node) {
  switch (node.kind) {
    case NodeKind::Composition:
      return std::holds_alternative<timeline::CompositionPayload>(node.payload);
    case NodeKind::Track:
      return std::holds_alternative<timeline::TrackPayload>(node.payload);
    case NodeKind::Clip:
      return std::holds_alternative<timeline::ClipPayload>(node.payload);
    case NodeKind::Camera:
      return std::holds_alternative<timeline::CameraPayload>(node.payload);
    case NodeKind::Effect:
      return std::holds_alternative<timeline::EffectPayload>(node.payload);
    case NodeKind::Asset:
      return std::holds_alternative<timeline::AssetPayload>(node.payload);
    case NodeKind::Note:
      return std::holds_alternative<timeline::NotePayload>(node.payload);
  }

  std::abort();
}

} // namespace

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

  if (!payloadMatchesNodeKind(node)) {
    return foundation::Error{"graph.node_payload_kind_mismatch", "Graph node payload must match graph node kind."};
  }

  nodes_.push_back(std::move(node));
  return {};
}

foundation::Result<void> GraphDocument::replaceNodePayload(foundation::NodeId nodeId, NodePayload payload) {
  const auto iterator = std::find_if(nodes_.begin(), nodes_.end(), [&](const GraphNode& node) {
    return node.id == nodeId;
  });

  if (iterator == nodes_.end()) {
    return foundation::Error{"graph.node_missing", "Graph node must exist before replacing its payload."};
  }

  GraphNode updated = *iterator;
  updated.payload = std::move(payload);
  if (!payloadMatchesNodeKind(updated)) {
    return foundation::Error{"graph.node_payload_kind_mismatch", "Graph node payload must match graph node kind."};
  }

  iterator->payload = std::move(updated.payload);
  return {};
}

foundation::Result<void> GraphDocument::removeNode(foundation::NodeId nodeId) {
  const auto nodeIterator = std::find_if(nodes_.begin(), nodes_.end(), [&](const GraphNode& node) {
    return node.id == nodeId;
  });

  if (nodeIterator == nodes_.end()) {
    return foundation::Error{"graph.node_missing", "Graph node must exist before removing it."};
  }

  nodes_.erase(nodeIterator);
  edges_.erase(
    std::remove_if(edges_.begin(), edges_.end(), [&](const GraphEdge& edge) {
      return edge.sourceNodeId == nodeId || edge.targetNodeId == nodeId;
    }),
    edges_.end()
  );
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

  if (edge.kind == EdgeKind::Connects && (edge.sourcePort.empty() || edge.targetPort.empty())) {
    return foundation::Error{"graph.edge_port_missing", "Connect edges require source and target ports."};
  }

  edges_.push_back(std::move(edge));
  return {};
}

} // namespace grapple::graph
