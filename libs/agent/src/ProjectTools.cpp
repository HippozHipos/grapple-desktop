#include <grapple/agent/ProjectTools.hpp>

#include <grapple/foundation/Json.hpp>
#include <grapple/project/ProjectQuery.hpp>

#include <sstream>

namespace grapple::agent {

AgentTool makeProjectInspectTool() {
  return AgentTool{
    foundation::ToolId{"tool_project_inspect"},
    "project.inspect",
    "Inspect Project",
    "Returns the current project revision and graph counts.",
    "ProjectInspectRequest",
    [](const ToolCall& call, AgentToolContext& context) -> foundation::Result<ToolResult> {
      auto query = context.queries.query(project::GetProjectSnapshotQuery{});
      if (!query) {
        return query.error();
      }

      const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&query.value());
      if (snapshotResult == nullptr) {
        return foundation::Error{"agent.project_snapshot_result_missing", "Project inspect query returned the wrong result type."};
      }

      const project::ProjectSnapshot& snapshot = snapshotResult->snapshot;
      std::ostringstream payload;
      payload << '{'
              << "\"projectId\":" << foundation::jsonQuoted(snapshot.info.id.value())
              << ",\"revision\":" << foundation::jsonQuoted(snapshot.revision.value())
              << ",\"revisionNumber\":" << snapshot.revisionNumber
              << ",\"canonicalHash\":" << foundation::jsonQuoted(snapshot.canonicalHash.toHex())
              << ",\"graph\":{\"nodes\":" << snapshot.graph.nodes().size()
              << ",\"edges\":" << snapshot.graph.edges().size()
              << "},\"assets\":{\"count\":" << snapshot.assets.assets().size()
              << "}}";

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        snapshot.revision,
        payload.str(),
        {}
      };
    }
  };
}

} // namespace grapple::agent
