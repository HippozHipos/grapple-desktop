#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace grapple::history {

struct CommandRecord {
  foundation::CommandId id;
  foundation::ProjectId projectId;
  foundation::RevisionId beforeRevision;
  foundation::RevisionId afterRevision;
  std::string serializedName;
  std::string serializedPayload;
  std::string sourceKind;
  std::optional<foundation::RunId> sourceRunId;
  std::string sourceActorName;
  std::chrono::system_clock::time_point createdAt;
};

} // namespace grapple::history
