#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <chrono>
#include <optional>

namespace grapple::agent {

enum class AgentRunStatus {
  Pending,
  Running,
  Succeeded,
  Failed
};

struct AgentRun {
  foundation::RunId id;
  foundation::ProjectId projectId;
  std::optional<foundation::RunId> parentRunId;
  AgentRunStatus status = AgentRunStatus::Pending;
  std::chrono::system_clock::time_point createdAt;
};

} // namespace grapple::agent
