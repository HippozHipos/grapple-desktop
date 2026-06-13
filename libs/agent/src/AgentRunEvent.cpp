#include <grapple/agent/AgentRunEvent.hpp>

#include <cstdlib>
#include <string_view>

namespace grapple::agent {

const char* serializedAgentRunEventKind(AgentRunEventKind kind) {
  switch (kind) {
    case AgentRunEventKind::RunStarted:
      return "run_started";
    case AgentRunEventKind::ModelMessage:
      return "model_message";
    case AgentRunEventKind::ToolCallStarted:
      return "tool_call_started";
    case AgentRunEventKind::ToolCallFinished:
      return "tool_call_finished";
    case AgentRunEventKind::DiagnosticEmitted:
      return "diagnostic_emitted";
    case AgentRunEventKind::RunFinished:
      return "run_finished";
    case AgentRunEventKind::DelegatedRunStarted:
      return "delegated_run_started";
    case AgentRunEventKind::DelegatedRunUpdated:
      return "delegated_run_updated";
    case AgentRunEventKind::DelegatedRunFinished:
      return "delegated_run_finished";
  }

  std::abort();
}

foundation::Result<AgentRunEventKind> parseAgentRunEventKind(std::string_view value) {
  if (value == "run_started") {
    return AgentRunEventKind::RunStarted;
  }
  if (value == "model_message") {
    return AgentRunEventKind::ModelMessage;
  }
  if (value == "tool_call_started") {
    return AgentRunEventKind::ToolCallStarted;
  }
  if (value == "tool_call_finished") {
    return AgentRunEventKind::ToolCallFinished;
  }
  if (value == "diagnostic_emitted") {
    return AgentRunEventKind::DiagnosticEmitted;
  }
  if (value == "run_finished") {
    return AgentRunEventKind::RunFinished;
  }
  if (value == "delegated_run_started") {
    return AgentRunEventKind::DelegatedRunStarted;
  }
  if (value == "delegated_run_updated") {
    return AgentRunEventKind::DelegatedRunUpdated;
  }
  if (value == "delegated_run_finished") {
    return AgentRunEventKind::DelegatedRunFinished;
  }
  return foundation::Error{"agent.run_event_kind_invalid", "Unknown agent run event kind."};
}

} // namespace grapple::agent
