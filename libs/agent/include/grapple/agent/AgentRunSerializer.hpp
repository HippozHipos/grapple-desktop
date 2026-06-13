#pragma once

#include <grapple/agent/AgentRun.hpp>
#include <grapple/foundation/Result.hpp>

#include <span>
#include <string>
#include <vector>

namespace grapple::agent {

std::string serializeCanonicalAgentRuns(std::span<const AgentRun> runs);
foundation::Result<std::vector<AgentRun>> deserializeCanonicalAgentRuns(const std::string& json);

} // namespace grapple::agent
