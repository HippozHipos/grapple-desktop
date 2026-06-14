#pragma once

#include <grapple/agent/AgentDiagnostic.hpp>
#include <grapple/agent/AgentToolContext.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <functional>
#include <string>
#include <utility>
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
  ToolResult(
    foundation::ToolId toolIdValue,
    ToolResultStatus statusValue,
    foundation::RevisionId observedRevisionValue,
    std::string payloadValue,
    std::vector<AgentDiagnostic> diagnosticsValue
  )
    : toolId{std::move(toolIdValue)},
      status{statusValue},
      observedRevision{std::move(observedRevisionValue)},
      payload{std::move(payloadValue)},
      diagnostics{std::move(diagnosticsValue)} {}

  foundation::ToolId toolId;
  ToolResultStatus status;
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
