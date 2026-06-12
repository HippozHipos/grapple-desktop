#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <optional>
#include <string>
#include <variant>

namespace grapple::project {

enum class CommandKind {
  CreateComposition,
  CreateTrack
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

using ProjectCommand = std::variant<
  CreateCompositionCommand,
  CreateTrackCommand
>;

struct ProjectCommandEnvelope {
  foundation::CommandId id;
  CommandKind kind = CommandKind::CreateComposition;
  foundation::ProjectId projectId;
  foundation::RevisionId expectedRevision;
  CommandSource source;
  ProjectCommand payload;
};

} // namespace grapple::project

