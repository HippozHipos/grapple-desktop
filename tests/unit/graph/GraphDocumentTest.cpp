#include <grapple/graph/GraphDocument.hpp>
#include <grapple/graph/GraphDiff.hpp>
#include <grapple/graph/GraphSerializer.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <TestAssert.hpp>

int main() {
  using namespace grapple;

  graph::GraphDocument graph;

  auto addComposition = graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_composition"},
    graph::NodeKind::Composition,
    timeline::CompositionPayload{"Main"},
    true
  });
  GRAPPLE_REQUIRE(addComposition);
  GRAPPLE_REQUIRE(graph.nodes().size() == 1);

  auto duplicateNode = graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_composition"},
    graph::NodeKind::Composition,
    timeline::CompositionPayload{"Duplicate"},
    true
  });
  GRAPPLE_REQUIRE(!duplicateNode);
  GRAPPLE_REQUIRE(duplicateNode.error().code == "graph.node_id_duplicate");

  auto mismatchedNodePayload = graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_payload_mismatch"},
    graph::NodeKind::Track,
    timeline::CompositionPayload{"Wrong payload"},
    true
  });
  GRAPPLE_REQUIRE(!mismatchedNodePayload);
  GRAPPLE_REQUIRE(mismatchedNodePayload.error().code == "graph.node_payload_kind_mismatch");
  GRAPPLE_REQUIRE(graph.nodes().size() == 1);

  auto missingEndpoint = graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_missing"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_composition"},
    foundation::NodeId{"node_missing"},
    true
  });
  GRAPPLE_REQUIRE(!missingEndpoint);
  GRAPPLE_REQUIRE(missingEndpoint.error().code == "graph.edge_node_missing");

  graph::GraphDocument next = graph;
  auto addTrack = next.addNode(graph::GraphNode{
    foundation::NodeId{"node_track"},
    graph::NodeKind::Track,
    timeline::TrackPayload{"Video"},
    true
  });
  GRAPPLE_REQUIRE(addTrack);

  auto addEdge = next.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_contains_track"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_composition"},
    foundation::NodeId{"node_track"},
    true
  });
  GRAPPLE_REQUIRE(addEdge);

  const graph::GraphDiff diff = graph::diffGraphs(graph, next);
  GRAPPLE_REQUIRE(diff.addedNodes.size() == 1);
  GRAPPLE_REQUIRE(diff.addedNodes[0] == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(diff.addedEdges.size() == 1);
  GRAPPLE_REQUIRE(diff.addedEdges[0] == foundation::EdgeId{"edge_contains_track"});
  GRAPPLE_REQUIRE(diff.removedNodes.empty());
  GRAPPLE_REQUIRE(diff.changedNodes.empty());

  graph::GraphDocument unorderedNext = graph;
  GRAPPLE_REQUIRE(unorderedNext.addNode(graph::GraphNode{
    foundation::NodeId{"node_z"},
    graph::NodeKind::Track,
    timeline::TrackPayload{"Z"},
    true
  }));
  GRAPPLE_REQUIRE(unorderedNext.addNode(graph::GraphNode{
    foundation::NodeId{"node_a"},
    graph::NodeKind::Track,
    timeline::TrackPayload{"A"},
    true
  }));

  const graph::GraphDiff orderedDiff = graph::diffGraphs(graph, unorderedNext);
  GRAPPLE_REQUIRE(orderedDiff.addedNodes.size() == 2);
  GRAPPLE_REQUIRE(orderedDiff.addedNodes[0] == foundation::NodeId{"node_a"});
  GRAPPLE_REQUIRE(orderedDiff.addedNodes[1] == foundation::NodeId{"node_z"});

  graph::GraphDocument sameRecordsDifferentOrder;
  GRAPPLE_REQUIRE(sameRecordsDifferentOrder.addNode(graph::GraphNode{
    foundation::NodeId{"node_track"},
    graph::NodeKind::Track,
    timeline::TrackPayload{"Video"},
    true
  }));
  GRAPPLE_REQUIRE(sameRecordsDifferentOrder.addNode(graph::GraphNode{
    foundation::NodeId{"node_composition"},
    graph::NodeKind::Composition,
    timeline::CompositionPayload{"Main"},
    true
  }));
  GRAPPLE_REQUIRE(sameRecordsDifferentOrder.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_contains_track"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_composition"},
    foundation::NodeId{"node_track"},
    true
  }));

  GRAPPLE_REQUIRE(graph::serializeCanonicalGraph(next) == graph::serializeCanonicalGraph(sameRecordsDifferentOrder));

  return 0;
}
