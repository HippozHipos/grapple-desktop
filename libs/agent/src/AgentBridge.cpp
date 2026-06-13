#include <grapple/agent/AgentBridge.hpp>

#include <grapple/foundation/Json.hpp>

#include <chrono>
#include <sstream>
#include <utility>

namespace grapple::agent {

namespace {

std::string toolCallStartedPayload(const AgentToolDispatchRequest& request) {
  std::ostringstream payload;
  payload << '{'
          << "\"toolCallId\":" << foundation::jsonQuoted(request.toolCallId.value())
          << ",\"toolSerializedId\":" << foundation::jsonQuoted(request.toolSerializedId)
          << ",\"argumentsJson\":" << foundation::jsonQuoted(request.argumentsJson)
          << '}';
  return payload.str();
}

std::string toolCallFinishedPayload(
  const foundation::ToolId& toolCallId,
  std::string status,
  std::string resultJson
) {
  std::ostringstream payload;
  payload << '{'
          << "\"toolCallId\":" << foundation::jsonQuoted(toolCallId.value())
          << ",\"status\":" << foundation::jsonQuoted(status)
          << ",\"resultJson\":" << foundation::jsonQuoted(resultJson)
          << '}';
  return payload.str();
}

std::string errorResultJson(const foundation::Error& error) {
  std::ostringstream payload;
  payload << '{'
          << "\"code\":" << foundation::jsonQuoted(error.code)
          << ",\"message\":" << foundation::jsonQuoted(error.message)
          << '}';
  return payload.str();
}

} // namespace

AgentBridge::AgentBridge(
  const AgentToolRegistry& tools,
  AgentToolContext& context,
  IAgentRunEventSink& events,
  std::int64_t& nextSequence
) : tools_{tools},
    context_{context},
    events_{events},
    nextSequence_{nextSequence} {}

foundation::Result<ToolResult> AgentBridge::dispatchToolCall(const AgentToolDispatchRequest& request) {
  auto started = appendEvent(
    request.runId,
    AgentRunEventKind::ToolCallStarted,
    toolCallStartedPayload(request)
  );
  if (!started) {
    return started.error();
  }

  const AgentTool* tool = tools_.findBySerializedId(request.toolSerializedId);
  if (tool == nullptr) {
    const foundation::Error error{
      "agent.tool_missing",
      "No registered agent tool matches " + request.toolSerializedId + "."
    };
    auto finished = appendEvent(
      request.runId,
      AgentRunEventKind::ToolCallFinished,
      toolCallFinishedPayload(request.toolCallId, "failed", errorResultJson(error))
    );
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto result = tool->handler(
    ToolCall{
      request.toolCallId,
      request.runId,
      request.projectId,
      request.expectedRevision,
      request.argumentsJson
    },
    context_
  );
  if (!result) {
    auto finished = appendEvent(
      request.runId,
      AgentRunEventKind::ToolCallFinished,
      toolCallFinishedPayload(request.toolCallId, "failed", errorResultJson(result.error()))
    );
    if (!finished) {
      return finished.error();
    }
    return result.error();
  }

  auto finished = appendEvent(
    request.runId,
    AgentRunEventKind::ToolCallFinished,
    toolCallFinishedPayload(request.toolCallId, "succeeded", result.value().payload)
  );
  if (!finished) {
    return finished.error();
  }

  return result.value();
}

foundation::Result<void> AgentBridge::appendEvent(
  foundation::RunId runId,
  AgentRunEventKind kind,
  std::string payloadJson
) {
  return events_.append(AgentRunEvent{
    std::move(runId),
    nextSequence_++,
    kind,
    std::move(payloadJson),
    std::chrono::system_clock::now()
  });
}

} // namespace grapple::agent
