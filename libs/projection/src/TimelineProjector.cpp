#include <grapple/projection/TimelineProjector.hpp>

#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/Payloads.hpp>

namespace grapple::projection {

namespace {

foundation::GraphId makeEffectGraphId(foundation::NodeId targetNodeId) {
  return foundation::GraphId{"effect_graph_" + targetNodeId.value()};
}

TimelineEffectGraph& getOrCreateEffectGraph(
  std::vector<TimelineEffectGraph>& effectGraphs,
  foundation::NodeId targetNodeId
) {
  for (TimelineEffectGraph& effectGraph : effectGraphs) {
    if (effectGraph.targetNodeId == targetNodeId) {
      return effectGraph;
    }
  }

  effectGraphs.push_back(TimelineEffectGraph{makeEffectGraphId(targetNodeId), targetNodeId, {}, {}});
  return effectGraphs.back();
}

bool containsNodeId(const std::vector<foundation::NodeId>& nodeIds, foundation::NodeId nodeId) {
  for (const foundation::NodeId& existing : nodeIds) {
    if (existing == nodeId) {
      return true;
    }
  }

  return false;
}

} // namespace

foundation::Result<BuildTimelineIRResult> TimelineProjector::buildTimelineIR(
  const BuildTimelineIRRequest& request
) const {
  const project::ProjectDocument& document = request.snapshot.document;

  TimelineIR timeline{
    document.info.id,
    document.revision,
    TimelineStage{document.info.name},
    foundation::TimeSeconds{0.0},
    {},
    {},
    {},
    {},
    {}
  };

  for (const graph::GraphNode& node : document.graph.nodes()) {
    if (node.kind == graph::NodeKind::Track) {
      const auto* payload = std::get_if<timeline::TrackPayload>(&node.payload);
      if (payload == nullptr) {
        return foundation::Error{"projection.track_payload_invalid", "Track node must carry a track payload."};
      }

      timeline.layers.push_back(TimelineLayer{node.id, payload->name, node.enabled});
    }

    if (node.kind == graph::NodeKind::Clip) {
      const auto* payload = std::get_if<timeline::ClipPayload>(&node.payload);
      if (payload == nullptr) {
        return foundation::Error{"projection.clip_payload_invalid", "Clip node must carry a clip payload."};
      }

      bool foundTrack = false;
      for (const graph::GraphEdge& edge : document.graph.edges()) {
        if (edge.kind == graph::EdgeKind::Contains && edge.targetNodeId == node.id) {
          const graph::GraphNode* track = document.graph.findNode(edge.sourceNodeId);
          if (track == nullptr || track->kind != graph::NodeKind::Track) {
            return foundation::Error{"projection.clip_track_invalid", "Clip containment source must be a track."};
          }

          timeline.clips.push_back(TimelineClip{node.id, edge.sourceNodeId, *payload, node.enabled && edge.enabled});
          foundTrack = true;
          break;
        }
      }

      if (!foundTrack) {
        return foundation::Error{"projection.clip_track_missing", "Clip must be contained by a track."};
      }
    }

    if (node.kind == graph::NodeKind::Camera) {
      const auto* payload = std::get_if<timeline::CameraPayload>(&node.payload);
      if (payload == nullptr) {
        return foundation::Error{"projection.camera_payload_invalid", "Camera node must carry a camera payload."};
      }

      timeline.cameras.push_back(TimelineCamera{node.id, payload->name, payload->transform, payload->lens, node.enabled});
    }

    if (node.kind == graph::NodeKind::Effect) {
      const auto* payload = std::get_if<timeline::EffectPayload>(&node.payload);
      if (payload == nullptr) {
        return foundation::Error{"projection.effect_payload_invalid", "Effect node must carry an effect payload."};
      }
    }
  }

  std::vector<foundation::NodeId> targetedEffectNodeIds;
  for (const graph::GraphEdge& edge : document.graph.edges()) {
    if (edge.kind != graph::EdgeKind::Targets) {
      continue;
    }

    const graph::GraphNode* effect = document.graph.findNode(edge.sourceNodeId);
    if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
      return foundation::Error{"projection.effect_source_invalid", "Effect target edge source must be an effect."};
    }

    if (!document.graph.hasNode(edge.targetNodeId)) {
      return foundation::Error{"projection.effect_target_missing", "Effect target edge target must exist."};
    }

    const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
    if (payload == nullptr) {
      return foundation::Error{"projection.effect_payload_invalid", "Effect node must carry an effect payload."};
    }

    TimelineEffectGraph& effectGraph = getOrCreateEffectGraph(timeline.effectGraphs, edge.targetNodeId);
    effectGraph.nodes.push_back(TimelineEffectNode{effect->id, *payload, effect->enabled});
    effectGraph.edges.push_back(TimelineEffectEdge{
      edge.id,
      edge.sourceNodeId,
      edge.sourcePort,
      edge.targetNodeId,
      edge.targetPort,
      edge.order,
      edge.enabled
    });
    targetedEffectNodeIds.push_back(edge.sourceNodeId);
  }

  for (const graph::GraphNode& node : document.graph.nodes()) {
    if (node.kind == graph::NodeKind::Effect && !containsNodeId(targetedEffectNodeIds, node.id)) {
      return foundation::Error{"projection.effect_target_missing", "Effect node must have a target edge."};
    }
  }

  return BuildTimelineIRResult{timeline, timeline.diagnostics};
}

} // namespace grapple::projection
