#include <grapple/project/ProjectController.hpp>

#include "internal/ProjectInvariants.hpp"

#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <variant>

namespace grapple::project {

namespace {

foundation::RevisionId makeRevisionId(std::int64_t revisionNumber) {
  return foundation::RevisionId{"rev_" + std::to_string(revisionNumber)};
}

bool sameParamValueType(const timeline::ParamValue& left, const timeline::ParamValue& right) {
  return left.index() == right.index();
}

foundation::Result<void> requireNonEmpty(std::string_view value, const char* code, const char* message) {
  if (value.empty()) {
    return foundation::Error{code, message};
  }
  return {};
}

template <typename Id>
foundation::Result<void> requireNonEmptyId(const Id& id, const char* code, const char* message) {
  return requireNonEmpty(id.value(), code, message);
}

} // namespace

ProjectController::ProjectController(ProjectDocument document)
  : document_(std::move(document)) {}

foundation::Result<ProjectCommandResult> ProjectController::apply(const ProjectCommandEnvelope& command) {
  auto commandId = requireNonEmptyId(command.id, "project.command_id_empty", "Command id must not be empty.");
  if (!commandId) {
    return commandId.error();
  }

  auto projectId = requireNonEmptyId(command.projectId, "project.command_project_id_empty", "Command project id must not be empty.");
  if (!projectId) {
    return projectId.error();
  }

  auto expectedRevision = requireNonEmptyId(command.expectedRevision, "project.command_expected_revision_empty", "Command expected revision must not be empty.");
  if (!expectedRevision) {
    return expectedRevision.error();
  }

  auto actorName = requireNonEmpty(command.source.actorName, "project.command_actor_empty", "Command actor name must not be empty.");
  if (!actorName) {
    return actorName.error();
  }

  if (command.source.runId.has_value()) {
    auto runId = requireNonEmptyId(*command.source.runId, "project.command_run_id_empty", "Command run id must not be empty when present.");
    if (!runId) {
      return runId.error();
    }
  }

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
  auto payloadShape = validateProjectCommandShape(command.payload);
  if (!payloadShape) {
    return payloadShape;
  }

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
      } else if constexpr (std::is_same_v<Command, ConnectPortsCommand>) {
        return handleConnectPorts(typedCommand);
      } else if constexpr (std::is_same_v<Command, DisconnectPortsCommand>) {
        return handleDisconnectPorts(typedCommand);
      } else if constexpr (std::is_same_v<Command, UpdateEffectParamValueCommand>) {
        return handleUpdateEffectParamValue(typedCommand);
      } else if constexpr (std::is_same_v<Command, UpsertEffectParamKeyframeCommand>) {
        return handleUpsertEffectParamKeyframe(typedCommand);
      } else if constexpr (std::is_same_v<Command, DeleteEffectParamKeyframeCommand>) {
        return handleDeleteEffectParamKeyframe(typedCommand);
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
        return ProjectQueryResult{GraphResult{document_.revision, document_.graph}};
      } else if constexpr (std::is_same_v<Query, GetAssetCatalogQuery>) {
        return ProjectQueryResult{AssetCatalogResult{document_.revision, document_.assets}};
      } else if constexpr (std::is_same_v<Query, InspectCompositionsQuery>) {
        auto result = inspectCompositions(makeProjectSnapshot(document_));
        if (!result) {
          return result.error();
        }
        return ProjectQueryResult{result.value()};
      } else if constexpr (std::is_same_v<Query, InspectRenderPlanQuery>) {
        return foundation::Error{
          "project.render_plan_query_requires_orchestration",
          "RenderPlan inspection must be handled by a query service that owns projection orchestration."
        };
      } else if constexpr (std::is_same_v<Query, InspectRuntimeDiagnosticsQuery>) {
        return foundation::Error{
          "project.runtime_diagnostics_query_requires_orchestration",
          "Runtime diagnostic inspection must be handled by a query service that owns runtime orchestration."
        };
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
    timeline::TrackPayload{command.name, command.kind},
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
  const auto* trackPayload = std::get_if<timeline::TrackPayload>(&track->payload);
  if (trackPayload == nullptr) {
    return foundation::Error{"project.track_payload_invalid", "Clip track must carry a track payload."};
  }
  auto trackKind = invariant::requireClipMatchesTrackKind(
    command.payload,
    *trackPayload,
    "project.clip_track_kind_mismatch",
    "Clip kind must match the containing track kind."
  );
  if (!trackKind) {
    return trackKind.error();
  }
  const asset::Asset* asset = document_.assets.find(command.payload.assetId);
  if (asset == nullptr) {
    return foundation::Error{"project.clip_asset_missing", "Clip asset must be registered before it can be placed on the timeline."};
  }
  auto assetKind = invariant::requireClipMatchesAssetMediaType(
    command.payload,
    *asset,
    "project.clip_asset_kind_mismatch",
    "Clip kind must match the registered asset media type."
  );
  if (!assetKind) {
    return assetKind.error();
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
  auto trackPayload = invariant::requireContainingTrackPayload(
    document_.graph,
    command.nodeId,
    invariant::ContainingTrackPayloadErrors{
      "project.clip_track_missing",
      "Clip must be contained by a track.",
      "project.clip_track_invalid",
      "Clip containment source must be a track.",
      "project.clip_track_invalid",
      "Track node must carry a track payload."
    }
  );
  if (!trackPayload) {
    return trackPayload.error();
  }
  auto trackKind = invariant::requireClipMatchesTrackKind(
    command.payload,
    *trackPayload.value(),
    "project.clip_track_kind_mismatch",
    "Clip kind must match the containing track kind."
  );
  if (!trackKind) {
    return trackKind.error();
  }
  const asset::Asset* asset = document_.assets.find(command.payload.assetId);
  if (asset == nullptr) {
    return foundation::Error{"project.clip_asset_missing", "Clip asset must be registered before a clip can reference it."};
  }
  auto assetKind = invariant::requireClipMatchesAssetMediaType(
    command.payload,
    *asset,
    "project.clip_asset_kind_mismatch",
    "Clip kind must match the registered asset media type."
  );
  if (!assetKind) {
    return assetKind.error();
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

foundation::Result<void> ProjectController::handleConnectPorts(const ConnectPortsCommand& command) {
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

foundation::Result<void> ProjectController::handleDisconnectPorts(const DisconnectPortsCommand& command) {
  return document_.graph.removeEdge(command.edgeId);
}

foundation::Result<void> ProjectController::handleUpdateEffectParamValue(
  const UpdateEffectParamValueCommand& command
) {
  const graph::GraphNode* effect = document_.graph.findNode(command.effectNodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_missing", "Effect param values can only be set on an existing effect node."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }

  timeline::EffectPayload updated = *payload;
  auto param = std::find_if(updated.params.values.begin(), updated.params.values.end(), [&](const timeline::Param& current) {
    return current.name == command.paramName;
  });
  if (param == updated.params.values.end()) {
    return foundation::Error{"project.effect_param_missing", "Effect param value command requires an existing effect parameter."};
  }
  if (!sameParamValueType(param->value, command.value)) {
    return foundation::Error{"project.effect_param_value_type_mismatch", "Effect param value must match the existing parameter value type."};
  }

  param->value = command.value;
  return document_.graph.replaceNodePayload(command.effectNodeId, std::move(updated));
}

foundation::Result<void> ProjectController::handleUpsertEffectParamKeyframe(
  const UpsertEffectParamKeyframeCommand& command
) {
  const graph::GraphNode* effect = document_.graph.findNode(command.effectNodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_missing", "Effect keyframes can only be set on an existing effect node."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }
  if (command.keyframe.time.value < 0.0) {
    return foundation::Error{"project.effect_keyframe_time_invalid", "Effect keyframe time cannot be negative."};
  }

  timeline::EffectPayload updated = *payload;
  auto param = std::find_if(updated.params.values.begin(), updated.params.values.end(), [&](const timeline::Param& current) {
    return current.name == command.paramName;
  });
  if (param == updated.params.values.end()) {
    return foundation::Error{"project.effect_param_missing", "Effect keyframe command requires an existing effect parameter."};
  }
  if (!sameParamValueType(param->value, command.keyframe.value)) {
    return foundation::Error{"project.effect_keyframe_value_type_mismatch", "Effect keyframe value must match the parameter value type."};
  }

  auto existingKeyframe = std::find_if(param->keyframes.begin(), param->keyframes.end(), [&](const timeline::Param::Keyframe& current) {
    return current.id == command.keyframe.id;
  });
  if (existingKeyframe == param->keyframes.end()) {
    param->keyframes.push_back(command.keyframe);
  } else {
    *existingKeyframe = command.keyframe;
  }

  return document_.graph.replaceNodePayload(command.effectNodeId, std::move(updated));
}

foundation::Result<void> ProjectController::handleDeleteEffectParamKeyframe(
  const DeleteEffectParamKeyframeCommand& command
) {
  const graph::GraphNode* effect = document_.graph.findNode(command.effectNodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_missing", "Effect keyframes can only be deleted on an existing effect node."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }

  timeline::EffectPayload updated = *payload;
  auto param = std::find_if(updated.params.values.begin(), updated.params.values.end(), [&](const timeline::Param& current) {
    return current.name == command.paramName;
  });
  if (param == updated.params.values.end()) {
    return foundation::Error{"project.effect_param_missing", "Effect keyframe command requires an existing effect parameter."};
  }

  auto keyframe = std::find_if(param->keyframes.begin(), param->keyframes.end(), [&](const timeline::Param::Keyframe& current) {
    return current.id == command.keyframeId;
  });
  if (keyframe == param->keyframes.end()) {
    return foundation::Error{"project.effect_keyframe_missing", "Effect keyframe delete requires an existing keyframe."};
  }
  param->keyframes.erase(keyframe);

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
  auto references = validateProjectSnapshotReferences(command.snapshot);
  if (!references) {
    return references.error();
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
