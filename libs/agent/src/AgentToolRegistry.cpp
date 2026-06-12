#include <grapple/agent/AgentToolRegistry.hpp>

#include <algorithm>

namespace grapple::agent {

foundation::Result<void> AgentToolRegistry::registerTool(AgentTool tool) {
  if (!tool.id) {
    return foundation::Error{"agent.tool_id_empty", "Agent tool id must not be empty."};
  }

  if (tool.serializedId.empty()) {
    return foundation::Error{"agent.tool_serialized_id_empty", "Agent tool serialized id must not be empty."};
  }

  const auto duplicate = std::any_of(tools_.begin(), tools_.end(), [&](const AgentTool& existing) {
    return existing.id == tool.id || existing.serializedId == tool.serializedId;
  });
  if (duplicate) {
    return foundation::Error{"agent.tool_duplicate", "Agent tool id or serialized id already exists."};
  }

  tools_.push_back(std::move(tool));
  return {};
}

const AgentTool* AgentToolRegistry::findBySerializedId(const std::string& serializedId) const noexcept {
  const auto iterator = std::find_if(tools_.begin(), tools_.end(), [&](const AgentTool& tool) {
    return tool.serializedId == serializedId;
  });

  if (iterator == tools_.end()) {
    return nullptr;
  }

  return &*iterator;
}

const std::vector<AgentTool>& AgentToolRegistry::tools() const noexcept {
  return tools_;
}

} // namespace grapple::agent

