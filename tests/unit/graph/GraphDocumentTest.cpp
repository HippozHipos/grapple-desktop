#include <grapple/graph/GraphDocument.hpp>
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

  auto missingEndpoint = graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_missing"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_composition"},
    foundation::NodeId{"node_missing"},
    true
  });
  GRAPPLE_REQUIRE(!missingEndpoint);
  GRAPPLE_REQUIRE(missingEndpoint.error().code == "graph.edge_node_missing");

  return 0;
}
