#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <chrono>
#include <string>

namespace grapple::history {

struct CommandRecord {
  foundation::CommandId id;
  foundation::ProjectId projectId;
  foundation::RevisionId beforeRevision;
  foundation::RevisionId afterRevision;
  std::string serializedName;
  std::string serializedPayload;
  std::chrono::system_clock::time_point createdAt;
};

} // namespace grapple::history

