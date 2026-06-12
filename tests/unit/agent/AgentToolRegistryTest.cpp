#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/model/ModelService.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

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
  const auto registeredCreateEffect = registry.registerTool(agent::makeProjectCreateEffectTool());
  GRAPPLE_REQUIRE(registeredCreateEffect);
  GRAPPLE_REQUIRE(registry.tools().size() == 2);
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.inspect") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.create_effect") != nullptr);

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
  const auto afterCommandSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterCommandSnapshot);

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
  GRAPPLE_REQUIRE(
    result.value().payload ==
    "{\"projectId\":\"proj_agent\",\"revision\":\"rev_1\",\"revisionNumber\":1,\"canonicalHash\":\"" +
      afterCommandSnapshot.value().canonicalHash.toHex() +
      "\",\"graph\":{\"nodes\":1,\"edges\":0},\"assets\":{\"count\":0}}"
  );

  const auto camera = project.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_camera"},
    foundation::ProjectId{"proj_agent"},
    command.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCameraCommand{
      foundation::NodeId{"node_camera"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_camera"},
      timeline::CameraPayload{"Camera", timeline::Transform{}, timeline::CameraLens{35.0}}
    }
  });
  GRAPPLE_REQUIRE(camera);

  const agent::AgentTool* createEffect = registry.findBySerializedId("project.create_effect");
  GRAPPLE_REQUIRE(createEffect != nullptr);
  const auto createEffectResult = createEffect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_project_create_effect"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      camera.value().afterRevision,
      R"({
        "targetNodeId": "node_camera",
        "displayName": "Center Subject",
        "implementationKind": "python",
        "language": "python",
        "entrypoint": "prepare",
        "source": "def prepare(ctx):\n  return {'camera_transform': ctx.camera.transform}\n",
        "sourcePort": "camera_transform",
        "targetPort": "input",
        "inputPorts": ["frame"],
        "outputPorts": ["camera_transform"],
        "activeRange": {"start": 0, "end": 10},
        "params": [
          {
            "name": "target_x",
            "label": "Target X",
            "value": 0.5,
            "numeric": {"min": 0, "max": 1, "step": 0.01}
          }
        ]
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(createEffectResult);
  GRAPPLE_REQUIRE(createEffectResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(createEffectResult.value().observedRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(createEffectResult.value().payload == "{\"commandId\":\"cmd_agent_create_effect_rev_3\",\"effectNodeId\":\"node_agent_effect_rev_3\",\"targetEdgeId\":\"edge_agent_effect_targets_rev_3\",\"targetNodeId\":\"node_camera\",\"revision\":\"rev_3\"}");

  const auto afterEffectSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterEffectSnapshot);
  const graph::GraphNode* effectNode = afterEffectSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_effect_rev_3"});
  GRAPPLE_REQUIRE(effectNode != nullptr);
  GRAPPLE_REQUIRE(effectNode->kind == graph::NodeKind::Effect);
  const graph::GraphEdge* effectEdge = nullptr;
  for (const graph::GraphEdge& edge : afterEffectSnapshot.value().graph.edges()) {
    if (edge.id == foundation::EdgeId{"edge_agent_effect_targets_rev_3"}) {
      effectEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(effectEdge != nullptr);
  GRAPPLE_REQUIRE(effectEdge->sourceNodeId == foundation::NodeId{"node_agent_effect_rev_3"});
  GRAPPLE_REQUIRE(effectEdge->targetNodeId == foundation::NodeId{"node_camera"});
  const auto* effectPayload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
  GRAPPLE_REQUIRE(effectPayload != nullptr);
  GRAPPLE_REQUIRE(effectPayload->displayName == "Center Subject");
  GRAPPLE_REQUIRE(effectPayload->implementation.kind == timeline::EffectImplementationKind::Python);
  GRAPPLE_REQUIRE(effectPayload->implementation.entrypoint == "prepare");
  GRAPPLE_REQUIRE(effectPayload->implementation.source.language == "python");
  GRAPPLE_REQUIRE(effectPayload->ports.inputs.size() == 1);
  GRAPPLE_REQUIRE(effectPayload->ports.outputs.size() == 1);
  GRAPPLE_REQUIRE(effectPayload->ports.outputs[0].name == "camera_transform");
  GRAPPLE_REQUIRE(effectPayload->params.values.size() == 1);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].name == "target_x");
  GRAPPLE_REQUIRE(std::get<double>(effectPayload->params.values[0].value) == 0.5);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.label == "Target X");
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric.has_value());
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric->min == 0.0);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric->max == 1.0);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric->step == 0.01);
  GRAPPLE_REQUIRE(effectPayload->activeRange.end == foundation::TimeSeconds{10.0});

  return 0;
}
