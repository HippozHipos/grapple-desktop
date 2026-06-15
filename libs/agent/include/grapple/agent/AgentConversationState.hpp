#pragma once

#include <grapple/agent/AgentDiagnostic.hpp>
#include <grapple/agent/AgentRun.hpp>
#include <grapple/agent/AgentRunEvent.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace grapple::agent {

struct AgentConversationMessage {
  foundation::RunId runId;
  std::int64_t sequence = 0;
  std::string role;
  std::string content;
};

enum class AgentConversationToolCallStatus {
  Running,
  Succeeded,
  Failed
};

struct AgentConversationToolCall {
  foundation::RunId runId;
  std::int64_t startedSequence = 0;
  std::int64_t finishedSequence = 0;
  foundation::ToolId toolCallId;
  std::string toolSerializedId;
  std::string toolDisplayName;
  std::string argumentsJson;
  AgentConversationToolCallStatus status = AgentConversationToolCallStatus::Running;
  std::string resultJson;
  std::optional<foundation::RevisionId> observedRevision;
};

struct AgentConversationDiagnostic {
  foundation::RunId runId;
  std::int64_t sequence = 0;
  std::string code;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string message;
};

struct AgentDelegatedRunState {
  foundation::RunId runId;
  std::int64_t startedSequence = 0;
  std::int64_t updatedSequence = 0;
  AgentRunStatus status = AgentRunStatus::Running;
  std::string label;
  std::string summary;
};

struct AgentConversationRunState {
  foundation::RunId runId;
  foundation::ProjectId projectId;
  std::optional<foundation::RunId> parentRunId;
  AgentRunStatus status = AgentRunStatus::Running;
  std::string title;
  std::string summary;
  std::vector<AgentConversationMessage> messages;
  std::vector<AgentConversationToolCall> toolCalls;
  std::vector<AgentConversationDiagnostic> diagnostics;
  std::vector<AgentDelegatedRunState> delegatedRuns;
};

struct AgentConversationProjectionDiagnostic {
  foundation::RunId runId;
  std::int64_t sequence = 0;
  std::string code;
  std::string message;
};

struct AgentConversationState {
  std::vector<AgentConversationRunState> runs;
  std::vector<AgentConversationProjectionDiagnostic> diagnostics;
};

class AgentConversationStateProjector {
public:
  [[nodiscard]] AgentConversationState project(
    std::span<const AgentRun> runs,
    std::span<const AgentRunEvent> events
  ) const;
};

} // namespace grapple::agent
