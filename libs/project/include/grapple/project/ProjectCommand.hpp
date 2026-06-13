#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace grapple::project {

enum class CommandKind {
  RegisterAsset,
  CreateComposition,
  CreateTrack,
  CreateClip,
  MoveClip,
  TrimClip,
  UpdateClip,
  DeleteClip,
  CreateCamera,
  UpdateCamera,
  CreateEffect,
  DeleteEffect,
  ConnectNodes,
  DisconnectNodes,
  UpdateEffectParams,
  CreateNote,
  UpdateNote,
  RestoreSnapshot
};

enum class CommandSourceKind {
  User,
  Agent,
  Importer,
  Migration
};

struct CommandSource {
  CommandSourceKind kind = CommandSourceKind::User;
  std::optional<foundation::RunId> runId;
  std::string actorName;
};

struct RegisterAssetCommand {
  asset::Asset asset;
};

struct CreateCompositionCommand {
  foundation::NodeId nodeId;
  std::string name;
};

struct CreateTrackCommand {
  foundation::NodeId nodeId;
  foundation::NodeId compositionNodeId;
  foundation::EdgeId containmentEdgeId;
  std::string name;
  std::int64_t order = 0;
};

struct CreateClipCommand {
  foundation::NodeId nodeId;
  foundation::NodeId trackNodeId;
  foundation::EdgeId containmentEdgeId;
  timeline::ClipPayload payload;
  std::int64_t order = 0;
};

struct MoveClipCommand {
  foundation::NodeId nodeId;
  foundation::TimeSeconds newStart;
};

struct TrimClipCommand {
  foundation::NodeId nodeId;
  foundation::TimeRange timelineRange;
  foundation::TimeRange sourceRange;
};

struct UpdateClipCommand {
  foundation::NodeId nodeId;
  timeline::ClipPayload payload;
};

struct DeleteClipCommand {
  foundation::NodeId nodeId;
};

struct CreateCameraCommand {
  foundation::NodeId nodeId;
  foundation::NodeId compositionNodeId;
  foundation::EdgeId containmentEdgeId;
  timeline::CameraPayload payload;
  std::int64_t order = 0;
};

struct UpdateCameraCommand {
  foundation::NodeId nodeId;
  timeline::CameraPayload payload;
};

struct CreateEffectCommand {
  foundation::NodeId nodeId;
  foundation::NodeId targetNodeId;
  foundation::EdgeId targetEdgeId;
  timeline::EffectPayload payload;
  graph::PortName sourcePort;
  graph::PortName targetPort;
  std::int64_t order = 0;
};

struct DeleteEffectCommand {
  foundation::NodeId nodeId;
};

struct ConnectNodesCommand {
  foundation::EdgeId edgeId;
  foundation::NodeId sourceNodeId;
  graph::PortName sourcePort;
  foundation::NodeId targetNodeId;
  graph::PortName targetPort;
  std::int64_t order = 0;
};

struct DisconnectNodesCommand {
  foundation::EdgeId edgeId;
};

struct UpdateEffectParamsCommand {
  foundation::NodeId effectNodeId;
  timeline::ParamSet params;
};

struct CreateNoteCommand {
  foundation::NodeId nodeId;
  timeline::NotePayload payload;
};

struct UpdateNoteCommand {
  foundation::NodeId nodeId;
  timeline::NotePayload payload;
};

struct RestoreSnapshotCommand {
  foundation::SnapshotId snapshotId;
  ProjectSnapshot snapshot;
};

using ProjectCommand = std::variant<
  RegisterAssetCommand,
  CreateCompositionCommand,
  CreateTrackCommand,
  CreateClipCommand,
  MoveClipCommand,
  TrimClipCommand,
  UpdateClipCommand,
  DeleteClipCommand,
  CreateCameraCommand,
  UpdateCameraCommand,
  CreateEffectCommand,
  DeleteEffectCommand,
  ConnectNodesCommand,
  DisconnectNodesCommand,
  UpdateEffectParamsCommand,
  CreateNoteCommand,
  UpdateNoteCommand,
  RestoreSnapshotCommand
>;

struct ProjectCommandEnvelope {
  foundation::CommandId id;
  foundation::ProjectId projectId;
  foundation::RevisionId expectedRevision;
  CommandSource source;
  ProjectCommand payload;
};

[[nodiscard]] CommandKind commandKind(const ProjectCommand& command);

} // namespace grapple::project
