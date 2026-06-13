#pragma once

#include <grapple/agent/AgentRunEvent.hpp>
#include <grapple/foundation/Result.hpp>

#include <span>
#include <string>
#include <vector>

namespace grapple::agent {

std::string serializeCanonicalAgentRunEvents(std::span<const AgentRunEvent> events);
foundation::Result<std::vector<AgentRunEvent>> deserializeCanonicalAgentRunEvents(const std::string& json);

} // namespace grapple::agent
