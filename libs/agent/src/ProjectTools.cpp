#include <grapple/agent/ProjectTools.hpp>

#include <grapple/project/ProjectDocument.hpp>
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

      const project::ProjectDocument& document = snapshotResult->snapshot.document;
      std::ostringstream payload;
      payload << "project=" << document.info.id.value()
              << "\nrevision=" << document.revision.value()
              << "\nnodes=" << document.graph.nodes().size()
              << "\nedges=" << document.graph.edges().size();

      return ToolResult{
        call.toolId,
        ToolResultStatus::Succeeded,
        document.revision,
        payload.str(),
        {}
      };
    }
  };
}

} // namespace grapple::agent

