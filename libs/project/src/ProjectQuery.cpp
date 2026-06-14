#include <grapple/project/ProjectQuery.hpp>

#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>

#include <algorithm>
#include <optional>
#include <variant>

namespace grapple::project {

namespace {

bool hasNodeId(const std::vector<foundation::NodeId>& nodeIds, const foundation::NodeId& nodeId) {
  return std::find(nodeIds.begin(), nodeIds.end(), nodeId) != nodeIds.end();
}

foundation::Result<CompositionClipSummary> inspectClip(
  const graph::GraphNode& node,
  const foundation::NodeId& trackNodeId
) {
  const auto* payload = std::get_if<timeline::ClipPayload>(&node.payload);
  if (payload == nullptr) {
    return foundation::Error{"project.clip_payload_invalid", "Clip node must carry a clip payload."};
  }

  return CompositionClipSummary{
    node.id,
    trackNodeId,
    payload->assetId,
    payload->kind,
    payload->timelineRange,
    node.enabled
  };
}

foundation::Result<CompositionTrackSummary> inspectTrack(
  const graph::GraphDocument& graph,
  const graph::GraphNode& node
) {
  const auto* payload = std::get_if<timeline::TrackPayload>(&node.payload);
  if (payload == nullptr) {
    return foundation::Error{"project.track_payload_invalid", "Track node must carry a track payload."};
  }

  CompositionTrackSummary track{node.id, payload->name, payload->kind, node.enabled, {}};
  for (const graph::GraphEdge& edge : graph.edges()) {
    if (edge.kind != graph::EdgeKind::Contains || edge.sourceNodeId != node.id) {
      continue;
    }

    const graph::GraphNode* clip = graph.findNode(edge.targetNodeId);
    if (clip == nullptr || clip->kind != graph::NodeKind::Clip) {
      continue;
    }

    auto inspectedClip = inspectClip(*clip, node.id);
    if (!inspectedClip) {
      return inspectedClip.error();
    }
    track.clips.push_back(inspectedClip.value());
  }

  return track;
}

foundation::Result<CompositionCameraSummary> inspectCamera(const graph::GraphNode& node) {
  const auto* payload = std::get_if<timeline::CameraPayload>(&node.payload);
  if (payload == nullptr) {
    return foundation::Error{"project.camera_payload_invalid", "Camera node must carry a camera payload."};
  }

  return CompositionCameraSummary{node.id, payload->name, node.enabled};
}

foundation::Result<CompositionEffectSummary> inspectEffect(
  const graph::GraphDocument& graph,
  const graph::GraphEdge& targetEdge
) {
  const graph::GraphNode* effect = graph.findNode(targetEdge.sourceNodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_source_invalid", "Effect target edge source must be an effect."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }

  return CompositionEffectSummary{
    effect->id,
    targetEdge.targetNodeId,
    payload->displayName,
    effect->enabled && targetEdge.enabled
  };
}

foundation::Result<CompositionSummary> inspectComposition(
  const graph::GraphDocument& graph,
  const graph::GraphNode& node
) {
  const auto* payload = std::get_if<timeline::CompositionPayload>(&node.payload);
  if (payload == nullptr) {
    return foundation::Error{"project.composition_payload_invalid", "Composition node must carry a composition payload."};
  }

  CompositionSummary composition{node.id, payload->name, node.enabled, {}, {}, {}};
  std::vector<foundation::NodeId> compositionNodeIds{node.id};

  for (const graph::GraphEdge& edge : graph.edges()) {
    if (edge.kind != graph::EdgeKind::Contains || edge.sourceNodeId != node.id) {
      continue;
    }

    const graph::GraphNode* child = graph.findNode(edge.targetNodeId);
    if (child == nullptr) {
      continue;
    }

    if (child->kind == graph::NodeKind::Track) {
      auto track = inspectTrack(graph, *child);
      if (!track) {
        return track.error();
      }
      for (const CompositionClipSummary& clip : track.value().clips) {
        compositionNodeIds.push_back(clip.nodeId);
      }
      compositionNodeIds.push_back(child->id);
      composition.tracks.push_back(std::move(track.value()));
    } else if (child->kind == graph::NodeKind::Camera) {
      auto camera = inspectCamera(*child);
      if (!camera) {
        return camera.error();
      }
      compositionNodeIds.push_back(child->id);
      composition.cameras.push_back(camera.value());
    }
  }

  for (const graph::GraphEdge& edge : graph.edges()) {
    if (edge.kind != graph::EdgeKind::Targets || !hasNodeId(compositionNodeIds, edge.targetNodeId)) {
      continue;
    }

    auto effect = inspectEffect(graph, edge);
    if (!effect) {
      return effect.error();
    }
    composition.effects.push_back(effect.value());
  }

  return composition;
}

} // namespace

foundation::Result<CompositionInspectResult> inspectCompositions(const ProjectSnapshot& snapshot) {
  CompositionInspectResult result{snapshot.revision, {}};

  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    if (node.kind != graph::NodeKind::Composition) {
      continue;
    }

    auto composition = inspectComposition(snapshot.graph, node);
    if (!composition) {
      return composition.error();
    }
    result.compositions.push_back(std::move(composition.value()));
  }

  return result;
}

foundation::Result<NotesResult> listNotes(const ProjectSnapshot& snapshot) {
  NotesResult result{snapshot.revision, {}};

  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    if (node.kind != graph::NodeKind::Note) {
      continue;
    }

    const auto* payload = std::get_if<timeline::NotePayload>(&node.payload);
    if (payload == nullptr) {
      return foundation::Error{"project.note_payload_invalid", "Note node must carry a note payload."};
    }

    result.notes.push_back(NoteSummary{
      node.id,
      payload->title,
      payload->markdown,
      node.enabled
    });
  }

  return result;
}

} // namespace grapple::project
