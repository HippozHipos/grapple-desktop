#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <chrono>
#include <optional>
#include <utility>

namespace grapple::agent {

enum class AgentRunStatus {
  Pending,
  Running,
  Succeeded,
  Failed
};

struct AgentRun {
  AgentRun(
    foundation::RunId idValue,
    foundation::ProjectId projectIdValue,
    std::optional<foundation::RunId> parentRunIdValue,
    AgentRunStatus statusValue,
    std::chrono::system_clock::time_point createdAtValue
  )
    : id{std::move(idValue)},
      projectId{std::move(projectIdValue)},
      parentRunId{std::move(parentRunIdValue)},
      status{statusValue},
      createdAt{createdAtValue} {}

  foundation::RunId id;
  foundation::ProjectId projectId;
  std::optional<foundation::RunId> parentRunId;
  AgentRunStatus status;
  std::chrono::system_clock::time_point createdAt;
};

} // namespace grapple::agent
