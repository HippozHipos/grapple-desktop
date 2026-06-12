#pragma once

#include <grapple/agent/AgentTool.hpp>
#include <grapple/foundation/Result.hpp>

#include <vector>

namespace grapple::agent {

class AgentToolRegistry {
public:
  foundation::Result<void> registerTool(AgentTool tool);

  [[nodiscard]] const AgentTool* findBySerializedId(const std::string& serializedId) const noexcept;
  [[nodiscard]] const std::vector<AgentTool>& tools() const noexcept;

private:
  std::vector<AgentTool> tools_;
};

} // namespace grapple::agent

