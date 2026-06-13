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
  const auto registeredAssetList = registry.registerTool(agent::makeAssetListTool());
  GRAPPLE_REQUIRE(registeredAssetList);
  const auto registeredCreateTrack = registry.registerTool(agent::makeTimelineCreateTrackTool());
  GRAPPLE_REQUIRE(registeredCreateTrack);
  const auto registeredCreateEffect = registry.registerTool(agent::makeEffectCreateNodeTool());
  GRAPPLE_REQUIRE(registeredCreateEffect);
  const auto registeredCreateNote = registry.registerTool(agent::makeNoteCreateTool());
  GRAPPLE_REQUIRE(registeredCreateNote);
  const auto registeredUpdateNote = registry.registerTool(agent::makeNoteUpdateTool());
  GRAPPLE_REQUIRE(registeredUpdateNote);
  GRAPPLE_REQUIRE(registry.tools().size() == 6);
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.inspect") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("asset.list") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.create_track") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.create_node") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.create_effect") == nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("note.create") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("note.update") != nullptr);
  const agent::AgentTool* registeredAssetListTool = registry.findBySerializedId("asset.list");
  GRAPPLE_REQUIRE(registeredAssetListTool != nullptr);
  GRAPPLE_REQUIRE(registeredAssetListTool->schema.find("\"additionalProperties\": false") != std::string::npos);
  GRAPPLE_REQUIRE(registeredAssetListTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateTrackTool = registry.findBySerializedId("timeline.create_track");
  GRAPPLE_REQUIRE(registeredCreateTrackTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateTrackTool->schema.find("\"compositionNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateTrackTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateEffectTool = registry.findBySerializedId("effect.create_node");
  GRAPPLE_REQUIRE(registeredCreateEffectTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"targetNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"params\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"minItems\": 1") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"numeric\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"commandId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"effectNodeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"targetEdgeId\"") == std::string::npos);

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

  const agent::AgentTool* createEffect = registry.findBySerializedId("effect.create_node");
  GRAPPLE_REQUIRE(createEffect != nullptr);
  const auto hiddenEffectResult = createEffect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_create_node"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      camera.value().afterRevision,
      R"({
        "targetNodeId": "node_camera",
        "displayName": "Hidden Effect",
        "implementationKind": "python",
        "language": "python",
        "entrypoint": "prepare",
        "source": "def prepare(ctx):\n  return {}\n",
        "sourcePort": "camera_transform",
        "targetPort": "input",
        "inputPorts": ["frame"],
        "outputPorts": ["camera_transform"],
        "activeRange": {"start": 0, "end": 10},
        "params": []
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(!hiddenEffectResult);
  GRAPPLE_REQUIRE(hiddenEffectResult.error().code == "agent.tool_arguments_invalid");
  GRAPPLE_REQUIRE(hiddenEffectResult.error().message.find("at least one editable parameter") != std::string::npos);

  const auto unlabeledEffectResult = createEffect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_create_node"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      camera.value().afterRevision,
      R"({
        "targetNodeId": "node_camera",
        "displayName": "Unlabeled Effect",
        "implementationKind": "python",
        "language": "python",
        "entrypoint": "prepare",
        "source": "def prepare(ctx):\n  return {}\n",
        "sourcePort": "camera_transform",
        "targetPort": "input",
        "inputPorts": ["frame"],
        "outputPorts": ["camera_transform"],
        "activeRange": {"start": 0, "end": 10},
        "params": [
          {
            "name": "target_x",
            "value": 0.5,
            "numeric": {"min": 0, "max": 1, "step": 0.01}
          }
        ]
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(!unlabeledEffectResult);
  GRAPPLE_REQUIRE(unlabeledEffectResult.error().code == "agent.tool_arguments_invalid");
  GRAPPLE_REQUIRE(unlabeledEffectResult.error().message.find("$.params[0].label") != std::string::npos);

  const auto createEffectResult = createEffect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_create_node"},
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

  const agent::AgentTool* createNote = registry.findBySerializedId("note.create");
  GRAPPLE_REQUIRE(createNote != nullptr);
  const auto createNoteResult = createNote->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_note_create"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      createEffectResult.value().observedRevision,
      R"({
        "title": "Edit rationale",
        "markdown": "Expose target_x as a user control."
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(createNoteResult);
  GRAPPLE_REQUIRE(createNoteResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(createNoteResult.value().observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(createNoteResult.value().payload == "{\"commandId\":\"cmd_agent_create_note_rev_4\",\"noteNodeId\":\"node_agent_note_rev_4\",\"revision\":\"rev_4\"}");

  const agent::AgentTool* updateNote = registry.findBySerializedId("note.update");
  GRAPPLE_REQUIRE(updateNote != nullptr);
  const auto updateNoteResult = updateNote->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_note_update"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      createNoteResult.value().observedRevision,
      R"({
        "nodeId": "node_agent_note_rev_4",
        "title": "Updated rationale",
        "markdown": "Parameter remains editable after creation."
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(updateNoteResult);
  GRAPPLE_REQUIRE(updateNoteResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(updateNoteResult.value().observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(updateNoteResult.value().payload == "{\"commandId\":\"cmd_agent_update_note_rev_5\",\"noteNodeId\":\"node_agent_note_rev_4\",\"revision\":\"rev_5\"}");
  const auto afterNoteSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterNoteSnapshot);
  const graph::GraphNode* noteNode = afterNoteSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_note_rev_4"});
  GRAPPLE_REQUIRE(noteNode != nullptr);
  GRAPPLE_REQUIRE(noteNode->kind == graph::NodeKind::Note);
  const auto* notePayload = std::get_if<timeline::NotePayload>(&noteNode->payload);
  GRAPPLE_REQUIRE(notePayload != nullptr);
  GRAPPLE_REQUIRE(notePayload->title == "Updated rationale");
  GRAPPLE_REQUIRE(notePayload->markdown == "Parameter remains editable after creation.");

  const auto missingNoteUpdateResult = updateNote->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_note_update"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      updateNoteResult.value().observedRevision,
      R"({
        "nodeId": "node_missing_note",
        "title": "Missing",
        "markdown": "No note exists."
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(!missingNoteUpdateResult);
  GRAPPLE_REQUIRE(missingNoteUpdateResult.error().code == "project.note_missing");

  const auto registerAsset = project.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_register_asset"},
    foundation::ProjectId{"proj_agent"},
    updateNoteResult.value().observedRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::RegisterAssetCommand{
      asset::Asset{
        foundation::AssetId{"asset_video"},
        "Walking Woman",
        asset::AssetMetadata{
          asset::AssetMediaType::Video,
          foundation::FilePath{"/media/walking-woman.mov"},
          foundation::FilePath{"/media/thumbs/walking-woman.jpg"},
          foundation::TimeSeconds{10.0},
          foundation::Resolution{1080, 1920},
          foundation::FrameRate{30000, 1001}
        }
      }
    }
  });
  GRAPPLE_REQUIRE(registerAsset);

  const agent::AgentTool* listAssets = registry.findBySerializedId("asset.list");
  GRAPPLE_REQUIRE(listAssets != nullptr);
  const auto listAssetsResult = listAssets->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_asset_list"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      registerAsset.value().afterRevision,
      ""
    },
    context
  );
  GRAPPLE_REQUIRE(listAssetsResult);
  GRAPPLE_REQUIRE(listAssetsResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(listAssetsResult.value().observedRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(
    listAssetsResult.value().payload ==
    "{\"revision\":\"rev_6\",\"assets\":[{\"assetId\":\"asset_video\",\"name\":\"Walking Woman\",\"mediaType\":\"video\",\"sourcePath\":\"/media/walking-woman.mov\",\"thumbnailPath\":\"/media/thumbs/walking-woman.jpg\",\"duration\":10,\"dimensions\":{\"width\":1080,\"height\":1920},\"frameRate\":{\"numerator\":30000,\"denominator\":1001}}]}"
  );

  const agent::AgentTool* createTrack = registry.findBySerializedId("timeline.create_track");
  GRAPPLE_REQUIRE(createTrack != nullptr);
  const auto createTrackResult = createTrack->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_create_track"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      registerAsset.value().afterRevision,
      R"({
        "compositionNodeId": "node_composition",
        "name": "Agent Track"
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(createTrackResult);
  GRAPPLE_REQUIRE(createTrackResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(createTrackResult.value().observedRevision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(createTrackResult.value().payload == "{\"commandId\":\"cmd_agent_create_track_rev_7\",\"trackNodeId\":\"node_agent_track_rev_7\",\"containmentEdgeId\":\"edge_agent_track_contains_rev_7\",\"compositionNodeId\":\"node_composition\",\"revision\":\"rev_7\"}");

  const auto afterTrackSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterTrackSnapshot);
  const graph::GraphNode* trackNode = afterTrackSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_track_rev_7"});
  GRAPPLE_REQUIRE(trackNode != nullptr);
  GRAPPLE_REQUIRE(trackNode->kind == graph::NodeKind::Track);
  const auto* trackPayload = std::get_if<timeline::TrackPayload>(&trackNode->payload);
  GRAPPLE_REQUIRE(trackPayload != nullptr);
  GRAPPLE_REQUIRE(trackPayload->name == "Agent Track");
  const graph::GraphEdge* trackEdge = nullptr;
  for (const graph::GraphEdge& edge : afterTrackSnapshot.value().graph.edges()) {
    if (edge.id == foundation::EdgeId{"edge_agent_track_contains_rev_7"}) {
      trackEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(trackEdge != nullptr);
  GRAPPLE_REQUIRE(trackEdge->kind == graph::EdgeKind::Contains);
  GRAPPLE_REQUIRE(trackEdge->sourceNodeId == foundation::NodeId{"node_composition"});
  GRAPPLE_REQUIRE(trackEdge->targetNodeId == foundation::NodeId{"node_agent_track_rev_7"});

  return 0;
}
