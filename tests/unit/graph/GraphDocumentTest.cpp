#include <grapple/graph/GraphDocument.hpp>
#include <grapple/graph/GraphDiff.hpp>
#include <grapple/graph/GraphSerializer.hpp>
#include <grapple/foundation/Hash.hpp>
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
  GRAPPLE_REQUIRE(diff.removedEdges.empty());
  GRAPPLE_REQUIRE(diff.changedEdges.empty());

  graph::GraphDocument changedNode = next;
  const auto changedTrackPayload = changedNode.replaceNodePayload(
    foundation::NodeId{"node_track"},
    timeline::TrackPayload{"Renamed Video"}
  );
  GRAPPLE_REQUIRE(changedTrackPayload);
  const graph::GraphDiff changedNodeDiff = graph::diffGraphs(next, changedNode);
  GRAPPLE_REQUIRE(changedNodeDiff.addedNodes.empty());
  GRAPPLE_REQUIRE(changedNodeDiff.removedNodes.empty());
  GRAPPLE_REQUIRE(changedNodeDiff.changedNodes.size() == 1);
  GRAPPLE_REQUIRE(changedNodeDiff.changedNodes[0] == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(changedNodeDiff.addedEdges.empty());
  GRAPPLE_REQUIRE(changedNodeDiff.removedEdges.empty());
  GRAPPLE_REQUIRE(changedNodeDiff.changedEdges.empty());

  graph::GraphDocument removedTrack = next;
  const auto removeTrack = removedTrack.removeNode(foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(removeTrack);
  GRAPPLE_REQUIRE(!removedTrack.hasNode(foundation::NodeId{"node_track"}));
  GRAPPLE_REQUIRE(removedTrack.edges().empty());
  const graph::GraphDiff removedTrackDiff = graph::diffGraphs(next, removedTrack);
  GRAPPLE_REQUIRE(removedTrackDiff.addedNodes.empty());
  GRAPPLE_REQUIRE(removedTrackDiff.removedNodes.size() == 1);
  GRAPPLE_REQUIRE(removedTrackDiff.removedNodes[0] == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(removedTrackDiff.changedNodes.empty());
  GRAPPLE_REQUIRE(removedTrackDiff.addedEdges.empty());
  GRAPPLE_REQUIRE(removedTrackDiff.removedEdges.size() == 2);
  GRAPPLE_REQUIRE(removedTrackDiff.removedEdges[0] == foundation::EdgeId{"edge_connect_ports"});
  GRAPPLE_REQUIRE(removedTrackDiff.removedEdges[1] == foundation::EdgeId{"edge_contains_track"});
  GRAPPLE_REQUIRE(removedTrackDiff.changedEdges.empty());
  const auto removeMissing = removedTrack.removeNode(foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(!removeMissing);
  GRAPPLE_REQUIRE(removeMissing.error().code == "graph.node_missing");

  graph::GraphDocument removedEdge = next;
  const auto removeConnectEdge = removedEdge.removeEdge(foundation::EdgeId{"edge_connect_ports"});
  GRAPPLE_REQUIRE(removeConnectEdge);
  GRAPPLE_REQUIRE(removedEdge.edges().size() == 1);
  GRAPPLE_REQUIRE(removedEdge.edges()[0].id == foundation::EdgeId{"edge_contains_track"});
  const auto removeMissingEdge = removedEdge.removeEdge(foundation::EdgeId{"edge_connect_ports"});
  GRAPPLE_REQUIRE(!removeMissingEdge);
  GRAPPLE_REQUIRE(removeMissingEdge.error().code == "graph.edge_missing");

  graph::GraphDocument changedEdge = next;
  GRAPPLE_REQUIRE(changedEdge.removeEdge(foundation::EdgeId{"edge_contains_track"}));
  GRAPPLE_REQUIRE(changedEdge.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_contains_track"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_composition"},
    graph::PortName{},
    foundation::NodeId{"node_track"},
    graph::PortName{},
    8,
    true
  }));
  const graph::GraphDiff changedEdgeDiff = graph::diffGraphs(next, changedEdge);
  GRAPPLE_REQUIRE(changedEdgeDiff.addedNodes.empty());
  GRAPPLE_REQUIRE(changedEdgeDiff.removedNodes.empty());
  GRAPPLE_REQUIRE(changedEdgeDiff.changedNodes.empty());
  GRAPPLE_REQUIRE(changedEdgeDiff.addedEdges.empty());
  GRAPPLE_REQUIRE(changedEdgeDiff.removedEdges.empty());
  GRAPPLE_REQUIRE(changedEdgeDiff.changedEdges.size() == 1);
  GRAPPLE_REQUIRE(changedEdgeDiff.changedEdges[0] == foundation::EdgeId{"edge_contains_track"});

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

  graph::GraphDocument payloadGraph;
  GRAPPLE_REQUIRE(payloadGraph.addNode(graph::GraphNode{
    foundation::NodeId{"node_clip_payload"},
    graph::NodeKind::Clip,
    timeline::ClipPayload{
      timeline::ClipKind::Video,
      foundation::TimeRange{foundation::TimeSeconds{2.0}, foundation::TimeSeconds{12.0}},
      foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{11.0}},
      0.5,
      foundation::AssetId{"asset_clip_payload"},
      timeline::Transform{
        foundation::Vec2{3.0, 4.0},
        foundation::Vec2{2.0, 2.0},
        15.0,
        0.8
      }
    },
    true
  }));
  GRAPPLE_REQUIRE(payloadGraph.addNode(graph::GraphNode{
    foundation::NodeId{"node_camera_payload"},
    graph::NodeKind::Camera,
    timeline::CameraPayload{
      "Camera",
      timeline::Transform{
        foundation::Vec2{1.0, 2.0},
        foundation::Vec2{1.5, 1.5},
        12.0,
        0.7
      },
      timeline::CameraLens{85.0}
    },
    true
  }));
  const std::string effectSource = "def prepare(ctx):\n  return {'x': 1}\n";
  GRAPPLE_REQUIRE(payloadGraph.addNode(graph::GraphNode{
    foundation::NodeId{"node_effect_payload"},
    graph::NodeKind::Effect,
    timeline::EffectPayload{
      "Effect",
      timeline::EffectImplementation{
        timeline::EffectImplementationKind::Python,
        "prepare",
        timeline::EffectSource{
          timeline::EffectSourceKind::InlineSource,
          "python",
          effectSource,
          std::nullopt,
          foundation::stableHash(effectSource)
        }
      },
      timeline::EffectPortSet{
        {timeline::EffectPort{"frame"}},
        {timeline::EffectPort{"camera"}}
      },
      timeline::ParamSet{
        {timeline::Param{"target_x", 0.77}}
      },
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}}
    },
    true
  }));
  const std::string serializedPayloadGraph = graph::serializeCanonicalGraph(payloadGraph);
  GRAPPLE_REQUIRE(serializedPayloadGraph.find("\"timelineRange\":{\"start\":2,\"end\":12}") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPayloadGraph.find("\"sourceRange\":{\"start\":1,\"end\":11}") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPayloadGraph.find("\"playbackRate\":0.5") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPayloadGraph.find("\"rotationDegrees\":15") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPayloadGraph.find("\"lens\":{\"focalLength\":85}") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPayloadGraph.find("\"inlineSource\":\"def prepare(ctx):\\n  return {'x': 1}\\n\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPayloadGraph.find("\"activeRange\":{\"start\":0,\"end\":10}") != std::string::npos);

  return 0;
}
