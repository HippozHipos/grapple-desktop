#include <grapple/project/ProjectCommand.hpp>

#include <string_view>
#include <type_traits>

namespace grapple::project {

namespace {

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

foundation::Result<void> validateClipPayload(const timeline::ClipPayload& payload) {
  return requireNonEmptyId(payload.assetId, "project.clip_asset_id_empty", "Clip asset id must not be empty.");
}

foundation::Result<void> validateAsset(const asset::Asset& asset) {
  auto assetId = requireNonEmptyId(asset.id, "asset.id_empty", "Asset id must not be empty.");
  if (!assetId) {
    return assetId;
  }
  auto name = requireNonEmpty(asset.name, "asset.name_empty", "Asset name must not be empty.");
  if (!name) {
    return name;
  }
  return requireNonEmpty(asset.metadata.sourcePath.value, "asset.source_path_empty", "Asset source path must not be empty.");
}

foundation::Result<void> validateCameraPayload(const timeline::CameraPayload& payload) {
  return requireNonEmpty(payload.name, "project.camera_name_empty", "Camera name must not be empty.");
}

foundation::Result<void> validateEffectPayload(const timeline::EffectPayload& payload) {
  auto displayName = requireNonEmpty(payload.displayName, "project.effect_display_name_empty", "Effect display name must not be empty.");
  if (!displayName) {
    return displayName;
  }
  auto entrypoint = requireNonEmpty(payload.implementation.entrypoint, "project.effect_entrypoint_empty", "Effect entrypoint must not be empty.");
  if (!entrypoint) {
    return entrypoint;
  }
  auto language = requireNonEmpty(payload.implementation.source.language, "project.effect_source_language_empty", "Effect source language must not be empty.");
  if (!language) {
    return language;
  }
  if (payload.implementation.source.sourceAssetId.has_value()) {
    auto sourceAssetId = requireNonEmptyId(
      *payload.implementation.source.sourceAssetId,
      "project.effect_source_asset_id_empty",
      "Effect source asset id must not be empty when present."
    );
    if (!sourceAssetId) {
      return sourceAssetId;
    }
  }
  for (const timeline::EffectPort& port : payload.ports.inputs) {
    auto portName = requireNonEmpty(port.name, "project.effect_port_name_empty", "Effect input port name must not be empty.");
    if (!portName) {
      return portName;
    }
  }
  for (const timeline::EffectPort& port : payload.ports.outputs) {
    auto portName = requireNonEmpty(port.name, "project.effect_port_name_empty", "Effect output port name must not be empty.");
    if (!portName) {
      return portName;
    }
  }
  for (const timeline::Param& param : payload.params.values) {
    auto paramName = requireNonEmpty(param.name, "project.effect_param_name_empty", "Effect parameter name must not be empty.");
    if (!paramName) {
      return paramName;
    }
    for (const timeline::Param::Keyframe& keyframe : param.keyframes) {
      auto keyframeId = requireNonEmptyId(keyframe.id, "project.effect_keyframe_id_empty", "Effect keyframe id must not be empty.");
      if (!keyframeId) {
        return keyframeId;
      }
    }
  }
  for (const timeline::EffectModelDependency& dependency : payload.modelDependencies) {
    auto modelId = requireNonEmptyId(dependency.modelId, "project.effect_model_id_empty", "Effect model id must not be empty.");
    if (!modelId) {
      return modelId;
    }
  }
  return {};
}

foundation::Result<void> validateNotePayload(const timeline::NotePayload& payload) {
  return requireNonEmpty(payload.title, "project.note_title_empty", "Note title must not be empty.");
}

foundation::Result<void> validateSnapshotHeader(const ProjectSnapshot& snapshot) {
  auto projectId = requireNonEmptyId(snapshot.info.id, "project.snapshot_project_id_empty", "Snapshot project id must not be empty.");
  if (!projectId) {
    return projectId;
  }
  auto name = requireNonEmpty(snapshot.info.name, "project.snapshot_name_empty", "Snapshot name must not be empty.");
  if (!name) {
    return name;
  }
  return requireNonEmptyId(snapshot.revision, "project.snapshot_revision_empty", "Snapshot revision must not be empty.");
}

} // namespace

CommandKind commandKind(const ProjectCommand& command) {
  return std::visit(
    [](const auto& typedCommand) -> CommandKind {
      using Command = std::decay_t<decltype(typedCommand)>;
      if constexpr (std::is_same_v<Command, RegisterAssetCommand>) {
        return CommandKind::RegisterAsset;
      } else if constexpr (std::is_same_v<Command, CreateCompositionCommand>) {
        return CommandKind::CreateComposition;
      } else if constexpr (std::is_same_v<Command, CreateTrackCommand>) {
        return CommandKind::CreateTrack;
      } else if constexpr (std::is_same_v<Command, DeleteTrackCommand>) {
        return CommandKind::DeleteTrack;
      } else if constexpr (std::is_same_v<Command, AddMediaToTimelineCommand>) {
        return CommandKind::AddMediaToTimeline;
      } else if constexpr (std::is_same_v<Command, CreateClipCommand>) {
        return CommandKind::CreateClip;
      } else if constexpr (std::is_same_v<Command, MoveClipCommand>) {
        return CommandKind::MoveClip;
      } else if constexpr (std::is_same_v<Command, TrimClipCommand>) {
        return CommandKind::TrimClip;
      } else if constexpr (std::is_same_v<Command, UpdateClipCommand>) {
        return CommandKind::UpdateClip;
      } else if constexpr (std::is_same_v<Command, DeleteClipCommand>) {
        return CommandKind::DeleteClip;
      } else if constexpr (std::is_same_v<Command, CreateCameraCommand>) {
        return CommandKind::CreateCamera;
      } else if constexpr (std::is_same_v<Command, UpdateCameraCommand>) {
        return CommandKind::UpdateCamera;
      } else if constexpr (std::is_same_v<Command, CreateEffectCommand>) {
        return CommandKind::CreateEffect;
      } else if constexpr (std::is_same_v<Command, DeleteEffectCommand>) {
        return CommandKind::DeleteEffect;
      } else if constexpr (std::is_same_v<Command, ConnectPortsCommand>) {
        return CommandKind::ConnectPorts;
      } else if constexpr (std::is_same_v<Command, DisconnectPortsCommand>) {
        return CommandKind::DisconnectPorts;
      } else if constexpr (std::is_same_v<Command, UpdateEffectParamValueCommand>) {
        return CommandKind::UpdateEffectParamValue;
      } else if constexpr (std::is_same_v<Command, UpsertEffectParamKeyframeCommand>) {
        return CommandKind::UpsertEffectParamKeyframe;
      } else if constexpr (std::is_same_v<Command, DeleteEffectParamKeyframeCommand>) {
        return CommandKind::DeleteEffectParamKeyframe;
      } else if constexpr (std::is_same_v<Command, CreateNoteCommand>) {
        return CommandKind::CreateNote;
      } else if constexpr (std::is_same_v<Command, UpdateNoteCommand>) {
        return CommandKind::UpdateNote;
      } else if constexpr (std::is_same_v<Command, RestoreSnapshotCommand>) {
        return CommandKind::RestoreSnapshot;
      }
    },
    command
  );
}

foundation::Result<void> validateProjectCommandShape(const ProjectCommand& command) {
  return std::visit(
    [](const auto& typedCommand) -> foundation::Result<void> {
      using Command = std::decay_t<decltype(typedCommand)>;
      if constexpr (std::is_same_v<Command, RegisterAssetCommand>) {
        return validateAsset(typedCommand.asset);
      } else if constexpr (std::is_same_v<Command, CreateCompositionCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        return requireNonEmpty(typedCommand.name, "project.composition_name_empty", "Composition name must not be empty.");
      } else if constexpr (std::is_same_v<Command, CreateTrackCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        auto compositionNodeId = requireNonEmptyId(typedCommand.compositionNodeId, "project.composition_node_id_empty", "Composition node id must not be empty.");
        if (!compositionNodeId) {
          return compositionNodeId;
        }
        auto name = requireNonEmpty(typedCommand.name, "project.track_name_empty", "Track name must not be empty.");
        if (!name) {
          return name;
        }
        return requireNonEmptyId(typedCommand.containmentEdgeId, "project.containment_edge_id_empty", "Containment edge id must not be empty.");
      } else if constexpr (std::is_same_v<Command, DeleteTrackCommand>) {
        return requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
      } else if constexpr (std::is_same_v<Command, AddMediaToTimelineCommand>) {
        if (typedCommand.composition.has_value()) {
          auto composition = validateProjectCommandShape(ProjectCommand{typedCommand.composition.value()});
          if (!composition) {
            return composition;
          }
        }
        if (typedCommand.track.has_value()) {
          auto track = validateProjectCommandShape(ProjectCommand{typedCommand.track.value()});
          if (!track) {
            return track;
          }
        }
        if (typedCommand.camera.has_value()) {
          auto camera = validateProjectCommandShape(ProjectCommand{typedCommand.camera.value()});
          if (!camera) {
            return camera;
          }
        }
        return validateProjectCommandShape(ProjectCommand{typedCommand.clip});
      } else if constexpr (std::is_same_v<Command, CreateClipCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        auto trackNodeId = requireNonEmptyId(typedCommand.trackNodeId, "project.track_node_id_empty", "Track node id must not be empty.");
        if (!trackNodeId) {
          return trackNodeId;
        }
        auto containmentEdgeId = requireNonEmptyId(typedCommand.containmentEdgeId, "project.containment_edge_id_empty", "Containment edge id must not be empty.");
        if (!containmentEdgeId) {
          return containmentEdgeId;
        }
        return validateClipPayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, MoveClipCommand>) {
        return requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
      } else if constexpr (std::is_same_v<Command, TrimClipCommand>) {
        return requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
      } else if constexpr (std::is_same_v<Command, UpdateClipCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        return validateClipPayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, DeleteClipCommand>) {
        return requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
      } else if constexpr (std::is_same_v<Command, CreateCameraCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        auto compositionNodeId = requireNonEmptyId(typedCommand.compositionNodeId, "project.composition_node_id_empty", "Composition node id must not be empty.");
        if (!compositionNodeId) {
          return compositionNodeId;
        }
        auto containmentEdgeId = requireNonEmptyId(typedCommand.containmentEdgeId, "project.containment_edge_id_empty", "Containment edge id must not be empty.");
        if (!containmentEdgeId) {
          return containmentEdgeId;
        }
        return validateCameraPayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, UpdateCameraCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        return validateCameraPayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, CreateEffectCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        auto targetNodeId = requireNonEmptyId(typedCommand.targetNodeId, "project.effect_target_node_id_empty", "Effect target node id must not be empty.");
        if (!targetNodeId) {
          return targetNodeId;
        }
        auto targetEdgeId = requireNonEmptyId(typedCommand.targetEdgeId, "project.effect_target_edge_id_empty", "Effect target edge id must not be empty.");
        if (!targetEdgeId) {
          return targetEdgeId;
        }
        auto sourcePort = requireNonEmpty(typedCommand.sourcePort.value, "project.effect_target_port_empty", "Effect source port must not be empty.");
        if (!sourcePort) {
          return sourcePort;
        }
        auto targetPort = requireNonEmpty(typedCommand.targetPort.value, "project.effect_target_port_empty", "Effect target port must not be empty.");
        if (!targetPort) {
          return targetPort;
        }
        return validateEffectPayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, DeleteEffectCommand>) {
        return requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
      } else if constexpr (std::is_same_v<Command, ConnectPortsCommand>) {
        auto edgeId = requireNonEmptyId(typedCommand.edgeId, "project.connect_edge_id_empty", "Connection edge id must not be empty.");
        if (!edgeId) {
          return edgeId;
        }
        auto sourceNodeId = requireNonEmptyId(typedCommand.sourceNodeId, "project.connect_node_id_empty", "Connection source node id must not be empty.");
        if (!sourceNodeId) {
          return sourceNodeId;
        }
        auto targetNodeId = requireNonEmptyId(typedCommand.targetNodeId, "project.connect_node_id_empty", "Connection target node id must not be empty.");
        if (!targetNodeId) {
          return targetNodeId;
        }
        auto sourcePort = requireNonEmpty(typedCommand.sourcePort.value, "project.connect_port_empty", "Connection source port must not be empty.");
        if (!sourcePort) {
          return sourcePort;
        }
        return requireNonEmpty(typedCommand.targetPort.value, "project.connect_port_empty", "Connection target port must not be empty.");
      } else if constexpr (std::is_same_v<Command, DisconnectPortsCommand>) {
        return requireNonEmptyId(typedCommand.edgeId, "project.edge_id_empty", "Command edge id must not be empty.");
      } else if constexpr (std::is_same_v<Command, UpdateEffectParamValueCommand>) {
        auto effectNodeId = requireNonEmptyId(typedCommand.effectNodeId, "project.effect_node_id_empty", "Effect node id must not be empty.");
        if (!effectNodeId) {
          return effectNodeId;
        }
        return requireNonEmpty(typedCommand.paramName, "project.effect_param_name_empty", "Effect parameter name must not be empty.");
      } else if constexpr (std::is_same_v<Command, UpsertEffectParamKeyframeCommand>) {
        auto effectNodeId = requireNonEmptyId(typedCommand.effectNodeId, "project.effect_node_id_empty", "Effect node id must not be empty.");
        if (!effectNodeId) {
          return effectNodeId;
        }
        auto paramName = requireNonEmpty(typedCommand.paramName, "project.effect_param_name_empty", "Effect parameter name must not be empty.");
        if (!paramName) {
          return paramName;
        }
        return requireNonEmptyId(typedCommand.keyframe.id, "project.effect_keyframe_id_empty", "Effect keyframe id must not be empty.");
      } else if constexpr (std::is_same_v<Command, DeleteEffectParamKeyframeCommand>) {
        auto effectNodeId = requireNonEmptyId(typedCommand.effectNodeId, "project.effect_node_id_empty", "Effect node id must not be empty.");
        if (!effectNodeId) {
          return effectNodeId;
        }
        auto paramName = requireNonEmpty(typedCommand.paramName, "project.effect_param_name_empty", "Effect parameter name must not be empty.");
        if (!paramName) {
          return paramName;
        }
        return requireNonEmptyId(typedCommand.keyframeId, "project.effect_keyframe_id_empty", "Effect keyframe id must not be empty.");
      } else if constexpr (std::is_same_v<Command, CreateNoteCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        return validateNotePayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, UpdateNoteCommand>) {
        auto nodeId = requireNonEmptyId(typedCommand.nodeId, "project.node_id_empty", "Command node id must not be empty.");
        if (!nodeId) {
          return nodeId;
        }
        return validateNotePayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, RestoreSnapshotCommand>) {
        auto snapshotId = requireNonEmptyId(typedCommand.snapshotId, "project.snapshot_id_empty", "Restore snapshot id must not be empty.");
        if (!snapshotId) {
          return snapshotId;
        }
        return validateSnapshotHeader(typedCommand.snapshot);
      }
    },
    command
  );
}

} // namespace grapple::project
