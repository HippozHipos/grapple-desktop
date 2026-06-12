#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/project/ProjectDocument.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <optional>
#include <string>
#include <variant>

namespace grapple::project {

enum class CommandKind {
  CreateComposition,
  CreateTrack,
  CreateClip,
  CreateCamera,
  CreateEffect,
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

struct CreateCompositionCommand {
  foundation::NodeId nodeId;
  std::string name;
};

struct CreateTrackCommand {
  foundation::NodeId nodeId;
  foundation::NodeId compositionNodeId;
  foundation::EdgeId containmentEdgeId;
  std::string name;
};

struct CreateClipCommand {
  foundation::NodeId nodeId;
  foundation::NodeId trackNodeId;
  foundation::EdgeId containmentEdgeId;
  timeline::ClipPayload payload;
};

struct CreateCameraCommand {
  foundation::NodeId nodeId;
  foundation::NodeId compositionNodeId;
  foundation::EdgeId containmentEdgeId;
  timeline::CameraPayload payload;
};

struct CreateEffectCommand {
  foundation::NodeId nodeId;
  foundation::NodeId targetNodeId;
  foundation::EdgeId targetEdgeId;
  timeline::EffectPayload payload;
};

struct RestoreSnapshotCommand {
  foundation::SnapshotId snapshotId;
  ProjectDocument document;
};

using ProjectCommand = std::variant<
  CreateCompositionCommand,
  CreateTrackCommand,
  CreateClipCommand,
  CreateCameraCommand,
  CreateEffectCommand,
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
