#include <grapple/agent/AgentRunEvent.hpp>

#include <cstdlib>

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

} // namespace grapple::agent
