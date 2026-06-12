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

  auto mismatchedReplacementPayload = graph.replaceNodePayload(
    foundation::NodeId{"node_composition"},
    timeline::TrackPayload{"Wrong replacement"}
  );
  GRAPPLE_REQUIRE(!mismatchedReplacementPayload);
  GRAPPLE_REQUIRE(mismatchedReplacementPayload.error().code == "graph.node_payload_kind_mismatch");

  auto replacementPayload = graph.replaceNodePayload(
    foundation::NodeId{"node_composition"},
    timeline::CompositionPayload{"Renamed Main"}
  );
  GRAPPLE_REQUIRE(replacementPayload);
  const auto* replacedComposition = graph.findNode(foundation::NodeId{"node_composition"});
  GRAPPLE_REQUIRE(replacedComposition != nullptr);
  const auto* replacedPayload = std::get_if<timeline::CompositionPayload>(&replacedComposition->payload);
  GRAPPLE_REQUIRE(replacedPayload != nullptr);
  GRAPPLE_REQUIRE(replacedPayload->name == "Renamed Main");

  auto missingEndpoint = graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_missing"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_composition"},
    graph::PortName{},
    foundation::NodeId{"node_missing"},
    graph::PortName{},
    0,
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
    graph::PortName{},
    foundation::NodeId{"node_track"},
    graph::PortName{},
    3,
    true
  });
  GRAPPLE_REQUIRE(addEdge);

  auto missingConnectPorts = next.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_connect_missing_ports"},
    graph::EdgeKind::Connects,
    foundation::NodeId{"node_composition"},
    graph::PortName{},
    foundation::NodeId{"node_track"},
    graph::PortName{"input"},
    0,
    true
  });
  GRAPPLE_REQUIRE(!missingConnectPorts);
  GRAPPLE_REQUIRE(missingConnectPorts.error().code == "graph.edge_port_missing");

  auto connectEdge = next.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_connect_ports"},
    graph::EdgeKind::Connects,
    foundation::NodeId{"node_composition"},
    graph::PortName{"output"},
    foundation::NodeId{"node_track"},
    graph::PortName{"input"},
    4,
    true
  });
  GRAPPLE_REQUIRE(connectEdge);

  const graph::GraphDiff diff = graph::diffGraphs(graph, next);
  GRAPPLE_REQUIRE(diff.addedNodes.size() == 1);
  GRAPPLE_REQUIRE(diff.addedNodes[0] == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(diff.addedEdges.size() == 2);
  GRAPPLE_REQUIRE(diff.addedEdges[0] == foundation::EdgeId{"edge_connect_ports"});
  GRAPPLE_REQUIRE(diff.addedEdges[1] == foundation::EdgeId{"edge_contains_track"});
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
    timeline::CompositionPayload{"Renamed Main"},
    true
  }));
  GRAPPLE_REQUIRE(sameRecordsDifferentOrder.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_connect_ports"},
    graph::EdgeKind::Connects,
    foundation::NodeId{"node_composition"},
    graph::PortName{"output"},
    foundation::NodeId{"node_track"},
    graph::PortName{"input"},
    4,
    true
  }));
  GRAPPLE_REQUIRE(sameRecordsDifferentOrder.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_contains_track"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_composition"},
    graph::PortName{},
    foundation::NodeId{"node_track"},
    graph::PortName{},
    3,
    true
  }));

  GRAPPLE_REQUIRE(graph::serializeCanonicalGraph(next) == graph::serializeCanonicalGraph(sameRecordsDifferentOrder));
  GRAPPLE_REQUIRE(graph::serializeCanonicalGraph(next).find("\"order\":3") != std::string::npos);
  GRAPPLE_REQUIRE(graph::serializeCanonicalGraph(next).find("\"sourcePort\":\"output\"") != std::string::npos);

  return 0;
}
