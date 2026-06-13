#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace grapple::agent {

enum class AgentRunEventKind {
  RunStarted,
  ModelMessage,
  ToolCallStarted,
  ToolCallFinished,
  DiagnosticEmitted,
  RunFinished,
  DelegatedRunStarted,
  DelegatedRunUpdated,
  DelegatedRunFinished
};

struct AgentRunEvent {
  foundation::RunId runId;
  std::int64_t sequence = 0;
  AgentRunEventKind kind = AgentRunEventKind::RunStarted;
  std::string payloadJson;
  std::chrono::system_clock::time_point createdAt;
};

const char* serializedAgentRunEventKind(AgentRunEventKind kind);

} // namespace grapple::agent
