#pragma once

#include <grapple/agent/AgentDiagnostic.hpp>
#include <grapple/agent/AgentToolContext.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <functional>
#include <string>
#include <vector>

namespace grapple::agent {

enum class ToolResultStatus {
  Succeeded,
  Failed
};

struct ToolCall {
  foundation::ToolId toolId;
  foundation::RunId runId;
  foundation::ProjectId projectId;
  foundation::RevisionId expectedRevision;
  std::string arguments;
};

struct ToolResult {
  foundation::ToolId toolId;
  ToolResultStatus status = ToolResultStatus::Succeeded;
  foundation::RevisionId observedRevision;
  std::string payload;
  std::vector<AgentDiagnostic> diagnostics;
};

using AgentToolHandler = std::function<foundation::Result<ToolResult>(
  const ToolCall& call,
  AgentToolContext& context
)>;

struct AgentTool {
  foundation::ToolId id;
  std::string serializedId;
  std::string displayName;
  std::string description;
  std::string schema;
  AgentToolHandler handler;
};

} // namespace grapple::agent

