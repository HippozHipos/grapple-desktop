#pragma once

#include <grapple/agent/AgentRunEventLog.hpp>
#include <grapple/agent/AgentTool.hpp>
#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/foundation/Result.hpp>

#include <cstdint>
#include <string>

namespace grapple::agent {

struct AgentToolDispatchRequest {
  foundation::RunId runId;
  foundation::ProjectId projectId;
  foundation::RevisionId expectedRevision;
  foundation::ToolId toolCallId;
  std::string toolSerializedId;
  std::string argumentsJson;
};

class AgentBridge {
public:
  AgentBridge(
    const AgentToolRegistry& tools,
    AgentToolContext& context,
    IAgentRunEventSink& events,
    std::int64_t& nextSequence
  );

  foundation::Result<ToolResult> dispatchToolCall(const AgentToolDispatchRequest& request);

private:
  foundation::Result<void> appendEvent(
    foundation::RunId runId,
    AgentRunEventKind kind,
    std::string payloadJson
  );

  const AgentToolRegistry& tools_;
  AgentToolContext& context_;
  IAgentRunEventSink& events_;
  std::int64_t& nextSequence_;
};

} // namespace grapple::agent
