#include <grapple/project/ProjectController.hpp>

#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <string>
#include <variant>

namespace grapple::project {

namespace {

foundation::RevisionId makeRevisionId(std::int64_t revisionNumber) {
  return foundation::RevisionId{"rev_" + std::to_string(revisionNumber)};
}

} // namespace

ProjectController::ProjectController(ProjectDocument document)
  : document_(std::move(document)) {}

foundation::Result<ProjectCommandResult> ProjectController::apply(const ProjectCommandEnvelope& command) {
  if (command.projectId != document_.info.id) {
    return foundation::Error{"project.id_mismatch", "Command project id does not match the open project."};
  }

  if (command.expectedRevision != document_.revision) {
    return foundation::Error{"project.expected_revision_mismatch", "Command expected revision does not match current revision."};
  }

  auto commandReady = validateCommand(command);
  if (!commandReady) {
    return commandReady.error();
  }

  const foundation::RevisionId beforeRevision = document_.revision;
  const std::int64_t beforeRevisionNumber = document_.revisionNumber;
  ProjectDocument nextDocument = document_;
  ProjectController nextController{nextDocument};

  const auto payloadResult = nextController.applyPayload(command.payload);
  if (!payloadResult) {
    return payloadResult.error();
  }

  nextController.document_.revisionNumber = beforeRevisionNumber + 1;
  nextController.document_.revision = nextController.nextRevisionId();
  document_ = std::move(nextController.document_);

  ProjectCommandResult result{
    command.id,
    beforeRevision,
    document_.revision,
    {
      ProjectCommandAppliedEvent{command.id, beforeRevision, document_.revision},
      ProjectChangedEvent{document_.info.id, beforeRevision, document_.revision}
    },
    {}
  };

  return result;
}

foundation::Result<ProjectSnapshot> ProjectController::snapshot() const {
  return makeProjectSnapshot(document_);
}

foundation::Result<ProjectQueryResult> ProjectController::query(const ProjectQuery& query) const {
  return readQuery(query);
}

foundation::RevisionId ProjectController::nextRevisionId() const {
  return makeRevisionId(document_.revisionNumber);
}

foundation::Result<void> ProjectController::validateCommand(const ProjectCommandEnvelope& command) const {
  if (command.source.kind != CommandSourceKind::Agent) {
    return {};
  }

  const auto* createEffect = std::get_if<CreateEffectCommand>(&command.payload);
  if (createEffect == nullptr) {
    return {};
  }

  if (createEffect->payload.params.values.empty()) {
    return foundation::Error{
      "project.agent_effect_params_missing",
      "Agent-created effects must expose at least one user-editable parameter."
    };
  }

  for (const timeline::Param& param : createEffect->payload.params.values) {
    if (param.control.label.empty()) {
      return foundation::Error{
        "project.agent_effect_param_label_missing",
        "Agent-created effect parameter " + param.name + " must expose a user-facing label."
      };
    }
    if (!std::holds_alternative<double>(param.value) || !param.control.numeric.has_value()) {
      return foundation::Error{
        "project.agent_effect_param_control_missing",
        "Agent-created effect parameter " + param.name + " must expose a numeric user control."
      };
    }
  }

  return {};
}

foundation::Result<void> ProjectController::applyPayload(const ProjectCommand& payload) {
  return std::visit(
    [&](const auto& typedCommand) -> foundation::Result<void> {
      using Command = std::decay_t<decltype(typedCommand)>;
      if constexpr (std::is_same_v<Command, RegisterAssetCommand>) {
        return handleRegisterAsset(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateCompositionCommand>) {
        return handleCreateComposition(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateTrackCommand>) {
        return handleCreateTrack(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateClipCommand>) {
        return handleCreateClip(typedCommand);
      } else if constexpr (std::is_same_v<Command, MoveClipCommand>) {
        return handleMoveClip(typedCommand);
      } else if constexpr (std::is_same_v<Command, TrimClipCommand>) {
        return handleTrimClip(typedCommand);
      } else if constexpr (std::is_same_v<Command, UpdateClipCommand>) {
        return handleUpdateClip(typedCommand);
      } else if constexpr (std::is_same_v<Command, DeleteClipCommand>) {
        return handleDeleteClip(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateCameraCommand>) {
        return handleCreateCamera(typedCommand);
      } else if constexpr (std::is_same_v<Command, UpdateCameraCommand>) {
        return handleUpdateCamera(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateEffectCommand>) {
        return handleCreateEffect(typedCommand);
      } else if constexpr (std::is_same_v<Command, DeleteEffectCommand>) {
        return handleDeleteEffect(typedCommand);
      } else if constexpr (std::is_same_v<Command, ConnectNodesCommand>) {
        return handleConnectNodes(typedCommand);
      } else if constexpr (std::is_same_v<Command, DisconnectNodesCommand>) {
        return handleDisconnectNodes(typedCommand);
      } else if constexpr (std::is_same_v<Command, SetEffectParamsCommand>) {
        return handleSetEffectParams(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateNoteCommand>) {
        return handleCreateNote(typedCommand);
      } else if constexpr (std::is_same_v<Command, UpdateNoteCommand>) {
        return handleUpdateNote(typedCommand);
      } else if constexpr (std::is_same_v<Command, RestoreSnapshotCommand>) {
        return handleRestoreSnapshot(typedCommand);
      }
    },
    payload
  );
}

foundation::Result<ProjectQueryResult> ProjectController::readQuery(const ProjectQuery& query) const {
  return std::visit(
    [&](const auto& typedQuery) -> foundation::Result<ProjectQueryResult> {
      using Query = std::decay_t<decltype(typedQuery)>;
      if constexpr (std::is_same_v<Query, GetProjectSnapshotQuery>) {
        return ProjectQueryResult{ProjectSnapshotResult{makeProjectSnapshot(document_)}};
      } else if constexpr (std::is_same_v<Query, GetGraphQuery>) {
        return ProjectQueryResult{GraphResult{document_.graph}};
      }
    },
    query
  );
}

foundation::Result<void> ProjectController::handleRegisterAsset(const RegisterAssetCommand& command) {
  return document_.assets.registerAsset(command.asset);
}

foundation::Result<void> ProjectController::handleCreateComposition(const CreateCompositionCommand& command) {
  graph::GraphNode node{
    command.nodeId,
    graph::NodeKind::Composition,
    timeline::CompositionPayload{command.name},
    true
  };

  return document_.graph.addNode(std::move(node));
}

foundation::Result<void> ProjectController::handleCreateTrack(const CreateTrackCommand& command) {
  const graph::GraphNode* composition = document_.graph.findNode(command.compositionNodeId);
  if (composition == nullptr || composition->kind != graph::NodeKind::Composition) {
    return foundation::Error{"project.composition_missing", "Track must be created inside an existing composition."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Track,
    timeline::TrackPayload{command.name},
    true
  });
  if (!nodeResult) {
    return nodeResult;
  }

  return document_.graph.addEdge(graph::GraphEdge{
    command.containmentEdgeId,
    graph::EdgeKind::Contains,
    command.compositionNodeId,
    graph::PortName{},
    command.nodeId,
    graph::PortName{},
    command.order,
    true
  });
}

foundation::Result<void> ProjectController::handleCreateClip(const CreateClipCommand& command) {
  const graph::GraphNode* track = document_.graph.findNode(command.trackNodeId);
  if (track == nullptr || track->kind != graph::NodeKind::Track) {
    return foundation::Error{"project.track_missing", "Clip must be created inside an existing track."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Clip,
    command.payload,
    true
  });
  if (!nodeResult) {
    return nodeResult;
  }

  return document_.graph.addEdge(graph::GraphEdge{
    command.containmentEdgeId,
    graph::EdgeKind::Contains,
    command.trackNodeId,
    graph::PortName{},
    command.nodeId,
    graph::PortName{},
    command.order,
    true
  });
}

foundation::Result<void> ProjectController::handleUpdateClip(const UpdateClipCommand& command) {
  const graph::GraphNode* clip = document_.graph.findNode(command.nodeId);
  if (clip == nullptr || clip->kind != graph::NodeKind::Clip) {
    return foundation::Error{"project.clip_missing", "Clip updates require an existing clip node."};
  }

  return document_.graph.replaceNodePayload(command.nodeId, command.payload);
}

foundation::Result<void> ProjectController::handleMoveClip(const MoveClipCommand& command) {
  const graph::GraphNode* clip = document_.graph.findNode(command.nodeId);
  if (clip == nullptr || clip->kind != graph::NodeKind::Clip) {
    return foundation::Error{"project.clip_missing", "Clip movement requires an existing clip node."};
  }

  const auto* payload = std::get_if<timeline::ClipPayload>(&clip->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.clip_payload_invalid", "Clip movement requires a clip payload."};
  }
  if (command.newStart.value < 0.0) {
    return foundation::Error{"project.clip_move_before_zero", "Clip movement cannot place a clip before timeline start."};
  }

  timeline::ClipPayload movedPayload = *payload;
  const double duration = movedPayload.timelineRange.duration();
  movedPayload.timelineRange = foundation::TimeRange{
    command.newStart,
    foundation::TimeSeconds{command.newStart.value + duration}
  };
  return document_.graph.replaceNodePayload(command.nodeId, movedPayload);
}

foundation::Result<void> ProjectController::handleTrimClip(const TrimClipCommand& command) {
  const graph::GraphNode* clip = document_.graph.findNode(command.nodeId);
  if (clip == nullptr || clip->kind != graph::NodeKind::Clip) {
    return foundation::Error{"project.clip_missing", "Clip trim requires an existing clip node."};
  }

  const auto* payload = std::get_if<timeline::ClipPayload>(&clip->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.clip_payload_invalid", "Clip trim requires a clip payload."};
  }
  if (command.timelineRange.start.value < 0.0 || command.timelineRange.end.value < command.timelineRange.start.value) {
    return foundation::Error{"project.clip_timeline_range_invalid", "Clip trim requires a non-negative timeline range with end after start."};
  }
  if (command.sourceRange.start.value < 0.0 || command.sourceRange.end.value < command.sourceRange.start.value) {
    return foundation::Error{"project.clip_source_range_invalid", "Clip trim requires a non-negative source range with end after start."};
  }

  timeline::ClipPayload trimmedPayload = *payload;
  trimmedPayload.timelineRange = command.timelineRange;
  trimmedPayload.sourceRange = command.sourceRange;
  return document_.graph.replaceNodePayload(command.nodeId, trimmedPayload);
}

foundation::Result<void> ProjectController::handleDeleteClip(const DeleteClipCommand& command) {
  const graph::GraphNode* clip = document_.graph.findNode(command.nodeId);
  if (clip == nullptr || clip->kind != graph::NodeKind::Clip) {
    return foundation::Error{"project.clip_missing", "Clip deletion requires an existing clip node."};
  }

  return document_.graph.removeNode(command.nodeId);
}

foundation::Result<void> ProjectController::handleCreateCamera(const CreateCameraCommand& command) {
  const graph::GraphNode* composition = document_.graph.findNode(command.compositionNodeId);
  if (composition == nullptr || composition->kind != graph::NodeKind::Composition) {
    return foundation::Error{"project.composition_missing", "Camera must be created inside an existing composition."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Camera,
    command.payload,
    true
  });
  if (!nodeResult) {
    return nodeResult;
  }

  return document_.graph.addEdge(graph::GraphEdge{
    command.containmentEdgeId,
    graph::EdgeKind::Contains,
    command.compositionNodeId,
    graph::PortName{},
    command.nodeId,
    graph::PortName{},
    command.order,
    true
  });
}

foundation::Result<void> ProjectController::handleUpdateCamera(const UpdateCameraCommand& command) {
  const graph::GraphNode* camera = document_.graph.findNode(command.nodeId);
  if (camera == nullptr || camera->kind != graph::NodeKind::Camera) {
    return foundation::Error{"project.camera_missing", "Camera updates require an existing camera node."};
  }

  return document_.graph.replaceNodePayload(command.nodeId, command.payload);
}

foundation::Result<void> ProjectController::handleCreateEffect(const CreateEffectCommand& command) {
  const graph::GraphNode* target = document_.graph.findNode(command.targetNodeId);
  if (target == nullptr) {
    return foundation::Error{"project.effect_target_missing", "Effect target node must exist."};
  }

  if (target->kind != graph::NodeKind::Track &&
      target->kind != graph::NodeKind::Clip &&
      target->kind != graph::NodeKind::Camera) {
    return foundation::Error{"project.effect_target_invalid", "Effects can target tracks, clips, or cameras."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Effect,
    command.payload,
    true
  });
  if (!nodeResult) {
    return nodeResult;
  }

  return document_.graph.addEdge(graph::GraphEdge{
    command.targetEdgeId,
    graph::EdgeKind::Targets,
    command.nodeId,
    command.sourcePort,
    command.targetNodeId,
    command.targetPort,
    command.order,
    true
  });
}

foundation::Result<void> ProjectController::handleDeleteEffect(const DeleteEffectCommand& command) {
  const graph::GraphNode* effect = document_.graph.findNode(command.nodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_missing", "Effect deletion requires an existing effect node."};
  }

  return document_.graph.removeNode(command.nodeId);
}

foundation::Result<void> ProjectController::handleConnectNodes(const ConnectNodesCommand& command) {
  return document_.graph.addEdge(graph::GraphEdge{
    command.edgeId,
    graph::EdgeKind::Connects,
    command.sourceNodeId,
    command.sourcePort,
    command.targetNodeId,
    command.targetPort,
    command.order,
    true
  });
}

foundation::Result<void> ProjectController::handleDisconnectNodes(const DisconnectNodesCommand& command) {
  return document_.graph.removeEdge(command.edgeId);
}

foundation::Result<void> ProjectController::handleSetEffectParams(const SetEffectParamsCommand& command) {
  const graph::GraphNode* effect = document_.graph.findNode(command.effectNodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_missing", "Effect params can only be set on an existing effect node."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }

  timeline::EffectPayload updated = *payload;
  updated.params = command.params;
  return document_.graph.replaceNodePayload(command.effectNodeId, std::move(updated));
}

foundation::Result<void> ProjectController::handleCreateNote(const CreateNoteCommand& command) {
  return document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Note,
    command.payload,
    true
  });
}

foundation::Result<void> ProjectController::handleUpdateNote(const UpdateNoteCommand& command) {
  const graph::GraphNode* note = document_.graph.findNode(command.nodeId);
  if (note == nullptr || note->kind != graph::NodeKind::Note) {
    return foundation::Error{"project.note_missing", "Note updates require an existing note node."};
  }

  return document_.graph.replaceNodePayload(command.nodeId, command.payload);
}

foundation::Result<void> ProjectController::handleRestoreSnapshot(const RestoreSnapshotCommand& command) {
  if (!command.snapshotId) {
    return foundation::Error{"project.snapshot_id_empty", "Restore snapshot id must not be empty."};
  }

  if (command.snapshot.info.id != document_.info.id) {
    return foundation::Error{"project.restore_project_id_mismatch", "Restore snapshot must match the open project."};
  }

  document_.info = command.snapshot.info;
  document_.settings = command.snapshot.settings;
  document_.assets = command.snapshot.assets;
  document_.graph = command.snapshot.graph;
  return {};
}

ProjectDocument createEmptyProject(foundation::ProjectId projectId, std::string name) {
  return ProjectDocument{
    ProjectInfo{std::move(projectId), std::move(name)},
    makeRevisionId(0),
    0,
    ProjectSettings{},
    asset::AssetCatalog{},
    graph::GraphDocument{}
  };
}

} // namespace grapple::project
