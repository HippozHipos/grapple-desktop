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

foundation::GraphId makeEffectGraphId(const foundation::NodeId& targetNodeId) {
  return foundation::GraphId{"effect_graph_" + targetNodeId.value()};
}

bool effectGraphHasNode(const EffectGraphSummary& effectGraph, const foundation::NodeId& nodeId) {
  for (const EffectGraphNodeSummary& node : effectGraph.nodes) {
    if (node.nodeId == nodeId) {
      return true;
    }
  }

  return false;
}

EffectGraphSummary* findEffectGraphForConnection(
  std::vector<EffectGraphSummary>& effectGraphs,
  const foundation::NodeId& sourceNodeId,
  const foundation::NodeId& targetNodeId
) {
  EffectGraphSummary* foundGraph = nullptr;
  for (EffectGraphSummary& effectGraph : effectGraphs) {
    if (effectGraphHasNode(effectGraph, sourceNodeId) && effectGraphHasNode(effectGraph, targetNodeId)) {
      if (foundGraph != nullptr) {
        return nullptr;
      }
      foundGraph = &effectGraph;
    }
  }

  return foundGraph;
}

EffectGraphSummary& getOrCreateEffectGraph(
  std::vector<EffectGraphSummary>& effectGraphs,
  const foundation::NodeId& targetNodeId
) {
  for (EffectGraphSummary& effectGraph : effectGraphs) {
    if (effectGraph.targetNodeId == targetNodeId) {
      return effectGraph;
    }
  }

  effectGraphs.push_back(EffectGraphSummary{makeEffectGraphId(targetNodeId), targetNodeId, {}, {}});
  return effectGraphs.back();
}

EffectGraphEdgeSummary inspectEffectEdge(const graph::GraphEdge& edge) {
  return EffectGraphEdgeSummary{
    edge.id,
    edge.sourceNodeId,
    edge.sourcePort,
    edge.targetNodeId,
    edge.targetPort,
    edge.order,
    edge.enabled
  };
}

std::vector<EffectGraphPortSummary> inspectEffectPorts(const std::vector<timeline::EffectPort>& ports) {
  std::vector<EffectGraphPortSummary> result;
  result.reserve(ports.size());
  for (const timeline::EffectPort& port : ports) {
    result.push_back(EffectGraphPortSummary{port.name});
  }
  return result;
}

std::vector<EffectGraphParamSummary> inspectEffectParams(const timeline::ParamSet& params) {
  std::vector<EffectGraphParamSummary> result;
  result.reserve(params.values.size());

  for (const timeline::Param& param : params.values) {
    std::vector<EffectGraphParamKeyframeSummary> keyframes;
    keyframes.reserve(param.keyframes.size());
    for (const timeline::Param::Keyframe& keyframe : param.keyframes) {
      keyframes.push_back(EffectGraphParamKeyframeSummary{
        keyframe.id,
        keyframe.time,
        keyframe.value
      });
    }

    result.push_back(EffectGraphParamSummary{
      param.name,
      param.value,
      param.control.label,
      param.control.numeric,
      std::move(keyframes)
    });
  }

  return result;
}

foundation::Result<EffectGraphNodeSummary> inspectEffectNode(const graph::GraphNode& node) {
  if (node.kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_node_invalid", "Effect graph nodes must be effect nodes."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&node.payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }

  return EffectGraphNodeSummary{
    node.id,
    payload->displayName,
    payload->implementation.kind,
    payload->implementation.entrypoint,
    payload->implementation.source.kind,
    payload->implementation.source.language,
    payload->implementation.source.inlineSource,
    payload->implementation.source.sourceAssetId,
    payload->implementation.source.sourceHash,
    inspectEffectPorts(payload->ports.inputs),
    inspectEffectPorts(payload->ports.outputs),
    inspectEffectParams(payload->params),
    payload->activeRange,
    node.enabled
  };
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

foundation::Result<EffectGraphsInspectResult> inspectEffectGraphs(const ProjectSnapshot& snapshot) {
  EffectGraphsInspectResult result{snapshot.revision, {}};
  std::vector<foundation::NodeId> targetedEffectNodeIds;

  for (const graph::GraphEdge& edge : snapshot.graph.edges()) {
    if (edge.kind != graph::EdgeKind::Targets) {
      continue;
    }

    const graph::GraphNode* effect = snapshot.graph.findNode(edge.sourceNodeId);
    if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
      return foundation::Error{"project.effect_source_invalid", "Effect target edge source must be an effect."};
    }
    if (!snapshot.graph.hasNode(edge.targetNodeId)) {
      return foundation::Error{"project.effect_target_missing", "Effect target edge target must exist."};
    }

    auto inspectedEffect = inspectEffectNode(*effect);
    if (!inspectedEffect) {
      return inspectedEffect.error();
    }

    EffectGraphSummary& effectGraph = getOrCreateEffectGraph(result.effectGraphs, edge.targetNodeId);
    effectGraph.nodes.push_back(std::move(inspectedEffect.value()));
    effectGraph.edges.push_back(inspectEffectEdge(edge));
    targetedEffectNodeIds.push_back(edge.sourceNodeId);
  }

  for (const graph::GraphEdge& edge : snapshot.graph.edges()) {
    if (edge.kind != graph::EdgeKind::Connects) {
      continue;
    }

    const graph::GraphNode* source = snapshot.graph.findNode(edge.sourceNodeId);
    const graph::GraphNode* target = snapshot.graph.findNode(edge.targetNodeId);
    if (source == nullptr || source->kind != graph::NodeKind::Effect ||
        target == nullptr || target->kind != graph::NodeKind::Effect) {
      return foundation::Error{
        "project.effect_connection_endpoint_invalid",
        "Effect connection edges must connect effect nodes."
      };
    }

    EffectGraphSummary* effectGraph = findEffectGraphForConnection(
      result.effectGraphs,
      edge.sourceNodeId,
      edge.targetNodeId
    );
    if (effectGraph == nullptr) {
      return foundation::Error{
        "project.effect_connection_graph_missing",
        "Effect connection endpoints must belong to one effect graph."
      };
    }

    effectGraph->edges.push_back(inspectEffectEdge(edge));
  }

  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    if (node.kind == graph::NodeKind::Effect && !hasNodeId(targetedEffectNodeIds, node.id)) {
      return foundation::Error{"project.effect_target_missing", "Effect node must have a target edge."};
    }
  }

  return result;
}

} // namespace grapple::project
