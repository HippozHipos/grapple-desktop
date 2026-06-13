#pragma once

#include <grapple/agent/AgentRunEvent.hpp>
#include <grapple/foundation/Result.hpp>

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

} // namespace grapple::agent
