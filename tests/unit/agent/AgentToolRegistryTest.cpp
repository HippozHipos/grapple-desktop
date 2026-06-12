#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/model/ModelService.hpp>
#include <grapple/project/ProjectController.hpp>

#include <TestAssert.hpp>

namespace {

class TestModelService final : public grapple::model::IModelService {
public:
  grapple::foundation::Result<grapple::model::ModelResponse> complete(
    const grapple::model::ModelRequest& request
  ) override {
    return grapple::model::ModelResponse{request.modelId, ""};
  }

  grapple::foundation::Result<grapple::model::VisionResponse> analyzeImage(
    const grapple::model::VisionRequest& request
  ) override {
    return grapple::model::VisionResponse{request.modelId, ""};
  }

  grapple::foundation::Result<grapple::model::SegmentationResponse> segment(
    const grapple::model::SegmentationRequest& request
  ) override {
    return grapple::model::SegmentationResponse{request.modelId, ""};
  }
};

} // namespace

int main() {
  using namespace grapple;

  agent::AgentToolRegistry registry;
  const auto registered = registry.registerTool(agent::makeProjectInspectTool());
  GRAPPLE_REQUIRE(registered);
  GRAPPLE_REQUIRE(registry.tools().size() == 1);
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.inspect") != nullptr);

  const auto duplicate = registry.registerTool(agent::makeProjectInspectTool());
  GRAPPLE_REQUIRE(!duplicate);
  GRAPPLE_REQUIRE(duplicate.error().code == "agent.tool_duplicate");

  project::ProjectController project{
    project::createEmptyProject(foundation::ProjectId{"proj_agent"}, "Agent Project")
  };

  const auto initial = project.snapshot();
  GRAPPLE_REQUIRE(initial);

  const auto command = project.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_composition"},
    foundation::ProjectId{"proj_agent"},
    initial.value().revision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_1"}, "agent"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(command);

  TestModelService models;
  agent::AgentToolContext context{project, project, models};

  const agent::AgentTool* inspect = registry.findBySerializedId("project.inspect");
  GRAPPLE_REQUIRE(inspect != nullptr);

  const auto result = inspect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_project_inspect"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      command.value().afterRevision,
      ""
    },
    context
  );
  GRAPPLE_REQUIRE(result);
  GRAPPLE_REQUIRE(result.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(result.value().observedRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(result.value().payload.find("project=proj_agent") != std::string::npos);
  GRAPPLE_REQUIRE(result.value().payload.find("nodes=1") != std::string::npos);

  return 0;
}
