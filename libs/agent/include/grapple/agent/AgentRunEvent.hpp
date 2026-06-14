#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

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
  AgentRunEvent(
    foundation::RunId runIdValue,
    std::int64_t sequenceValue,
    AgentRunEventKind kindValue,
    std::string payloadJsonValue,
    std::chrono::system_clock::time_point createdAtValue
  )
    : runId{std::move(runIdValue)},
      sequence{sequenceValue},
      kind{kindValue},
      payloadJson{std::move(payloadJsonValue)},
      createdAt{createdAtValue} {}

  foundation::RunId runId;
  std::int64_t sequence;
  AgentRunEventKind kind;
  std::string payloadJson;
  std::chrono::system_clock::time_point createdAt;
};

const char* serializedAgentRunEventKind(AgentRunEventKind kind);
foundation::Result<AgentRunEventKind> parseAgentRunEventKind(std::string_view value);

} // namespace grapple::agent
