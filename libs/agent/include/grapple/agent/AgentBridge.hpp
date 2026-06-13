#pragma once

#include <grapple/agent/AgentRunEvent.hpp>
#include <grapple/agent/AgentTool.hpp>
#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/foundation/Result.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace grapple::agent {

class IAgentRunEventSink {
public:
  virtual ~IAgentRunEventSink() = default;

  virtual foundation::Result<void> append(AgentRunEvent event) = 0;
};

class AgentRunEventLog final : public IAgentRunEventSink {
public:
  foundation::Result<void> append(AgentRunEvent event) override;

  [[nodiscard]] const std::vector<AgentRunEvent>& records() const noexcept;

private:
  std::vector<AgentRunEvent> records_;
};

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
    IAgentRunEventSink& events
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
  std::int64_t nextSequence_ = 1;
};

} // namespace grapple::agent
