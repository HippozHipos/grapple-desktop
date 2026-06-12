#include <grapple/graph/GraphDiff.hpp>

#include <algorithm>

namespace grapple::graph {

namespace {

const GraphNode* findNode(const std::vector<GraphNode>& nodes, foundation::NodeId id) {
  const auto iterator = std::find_if(nodes.begin(), nodes.end(), [&](const GraphNode& node) {
    return node.id == id;
  });
  return iterator == nodes.end() ? nullptr : &*iterator;
}

const GraphEdge* findEdge(const std::vector<GraphEdge>& edges, foundation::EdgeId id) {
  const auto iterator = std::find_if(edges.begin(), edges.end(), [&](const GraphEdge& edge) {
    return edge.id == id;
  });
  return iterator == edges.end() ? nullptr : &*iterator;
}

bool nodeRecordChanged(const GraphNode& before, const GraphNode& after) {
  return before.kind != after.kind || before.enabled != after.enabled || before.payload != after.payload;
}

bool edgeRecordChanged(const GraphEdge& before, const GraphEdge& after) {
  return before.kind != after.kind ||
         before.sourceNodeId != after.sourceNodeId ||
         before.targetNodeId != after.targetNodeId ||
         before.enabled != after.enabled;
}

template <typename Id>
void sortIds(std::vector<Id>& ids) {
  std::sort(ids.begin(), ids.end());
}

} // namespace

GraphDiff diffGraphs(const GraphDocument& before, const GraphDocument& after) {
  GraphDiff diff;

  for (const GraphNode& afterNode : after.nodes()) {
    const GraphNode* beforeNode = findNode(before.nodes(), afterNode.id);
    if (beforeNode == nullptr) {
      diff.addedNodes.push_back(afterNode.id);
    } else if (nodeRecordChanged(*beforeNode, afterNode)) {
      diff.changedNodes.push_back(afterNode.id);
    }
  }

  for (const GraphNode& beforeNode : before.nodes()) {
    if (findNode(after.nodes(), beforeNode.id) == nullptr) {
      diff.removedNodes.push_back(beforeNode.id);
    }
  }

  for (const GraphEdge& afterEdge : after.edges()) {
    const GraphEdge* beforeEdge = findEdge(before.edges(), afterEdge.id);
    if (beforeEdge == nullptr) {
      diff.addedEdges.push_back(afterEdge.id);
    } else if (edgeRecordChanged(*beforeEdge, afterEdge)) {
      diff.changedEdges.push_back(afterEdge.id);
    }
  }

  for (const GraphEdge& beforeEdge : before.edges()) {
    if (findEdge(after.edges(), beforeEdge.id) == nullptr) {
      diff.removedEdges.push_back(beforeEdge.id);
    }
  }

  sortIds(diff.addedNodes);
  sortIds(diff.removedNodes);
  sortIds(diff.changedNodes);
  sortIds(diff.addedEdges);
  sortIds(diff.removedEdges);
  sortIds(diff.changedEdges);

  return diff;
}

} // namespace grapple::graph
