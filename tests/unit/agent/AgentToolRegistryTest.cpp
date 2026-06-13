#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <TestAssert.hpp>

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

namespace {

class TestAgentCommandService final : public grapple::project::IProjectCommandService {
public:
  explicit TestAgentCommandService(grapple::project::ProjectController& project)
    : project_{project} {}

  grapple::foundation::Result<grapple::project::ProjectCommandResult> apply(
    const grapple::project::ProjectCommandEnvelope& command
  ) override {
    ++applyCount_;
    return project_.apply(command);
  }

  [[nodiscard]] std::size_t applyCount() const noexcept {
    return applyCount_;
  }

private:
  grapple::project::ProjectController& project_;
  std::size_t applyCount_ = 0;
};

class TestAgentQueryService final : public grapple::project::IProjectQueryService {
public:
  explicit TestAgentQueryService(const grapple::project::ProjectController& project)
    : project_{project} {}

  grapple::foundation::Result<grapple::project::ProjectQueryResult> query(
    const grapple::project::ProjectQuery& query
  ) const override {
    ++totalQueryCount_;
    return std::visit(
      [&](const auto& typedQuery) -> grapple::foundation::Result<grapple::project::ProjectQueryResult> {
        using Query = std::decay_t<decltype(typedQuery)>;
        if constexpr (std::is_same_v<Query, grapple::project::InspectRenderPlanQuery>) {
          ++renderPlanQueryCount_;
          auto snapshot = project_.snapshot();
          if (!snapshot) {
            return snapshot.error();
          }
          return grapple::project::ProjectQueryResult{
            grapple::project::RenderPlanInspectResult{
              snapshot.value().info.id,
              snapshot.value().revision,
              grapple::foundation::TimeSeconds{5.0},
              snapshot.value().assets.assets().size(),
              {
                grapple::project::RenderPlanLayerSummary{
                  grapple::foundation::NodeId{"node_agent_track_rev_7"},
                  "Agent Track"
                }
              },
              {
                grapple::project::RenderPlanClipSummary{
                  grapple::foundation::NodeId{"node_agent_clip_rev_8"},
                  grapple::foundation::NodeId{"node_agent_track_rev_7"},
                  grapple::foundation::AssetId{"asset_video"},
                  grapple::timeline::ClipKind::Video,
                  grapple::foundation::TimeRange{
                    grapple::foundation::TimeSeconds{3.0},
                    grapple::foundation::TimeSeconds{5.0}
                  }
                }
              },
              {
                grapple::project::RenderPlanCameraSummary{
                  grapple::foundation::NodeId{"node_camera"},
                  "Camera"
                }
              },
              {
                grapple::project::RenderPlanEffectGraphSummary{
                  grapple::foundation::GraphId{"effect_graph_node_camera"},
                  grapple::foundation::NodeId{"node_camera"},
                  1,
                  1
                }
              },
              0
            }
          };
        } else if constexpr (std::is_same_v<Query, grapple::project::InspectRuntimeDiagnosticsQuery>) {
          ++runtimeDiagnosticsQueryCount_;
          auto snapshot = project_.snapshot();
          if (!snapshot) {
            return snapshot.error();
          }
          return grapple::project::ProjectQueryResult{
            grapple::project::RuntimeInspectDiagnosticsResult{
              snapshot.value().revision,
              {
                grapple::project::RuntimeDiagnosticSummary{
                  "runtime.effect_runtime_missing",
                  grapple::project::RuntimeDiagnosticSeveritySummary::Warning,
                  snapshot.value().info.id,
                  snapshot.value().revision,
                  grapple::foundation::NodeId{"node_agent_effect_rev_3"},
                  "No runtime registered for this effect implementation."
                }
              }
            }
          };
        } else if constexpr (std::is_same_v<Query, grapple::project::GetProjectSnapshotQuery>) {
          ++snapshotQueryCount_;
          return project_.query(typedQuery);
        } else if constexpr (std::is_same_v<Query, grapple::project::InspectCompositionsQuery>) {
          ++compositionQueryCount_;
          return project_.query(typedQuery);
        } else {
          return project_.query(typedQuery);
        }
      },
      query
    );
  }

  [[nodiscard]] std::size_t totalQueryCount() const noexcept {
    return totalQueryCount_;
  }

  [[nodiscard]] std::size_t snapshotQueryCount() const noexcept {
    return snapshotQueryCount_;
  }

  [[nodiscard]] std::size_t compositionQueryCount() const noexcept {
    return compositionQueryCount_;
  }

  [[nodiscard]] std::size_t renderPlanQueryCount() const noexcept {
    return renderPlanQueryCount_;
  }

  [[nodiscard]] std::size_t runtimeDiagnosticsQueryCount() const noexcept {
    return runtimeDiagnosticsQueryCount_;
  }

private:
  const grapple::project::ProjectController& project_;
  mutable std::size_t totalQueryCount_ = 0;
  mutable std::size_t snapshotQueryCount_ = 0;
  mutable std::size_t compositionQueryCount_ = 0;
  mutable std::size_t renderPlanQueryCount_ = 0;
  mutable std::size_t runtimeDiagnosticsQueryCount_ = 0;
};

bool allUnique(std::vector<std::string> values) {
  std::sort(values.begin(), values.end());
  return std::adjacent_find(values.begin(), values.end()) == values.end();
}

void requireNoRuntimeOwnedFields(const std::string& payload) {
  const std::vector<std::string> disallowedFields{
    "\"frameHandle\"",
    "\"textureHandle\"",
    "\"maskHandle\"",
    "\"cacheHandle\"",
    "\"rgbaPixels\"",
    "\"sampledFrame\"",
    "\"preparedPlan\"",
    "\"resolvedCamera\"",
    "\"resolvedLayers\"",
    "\"motionVectors\"",
    "\"depthTexture\"",
    "\"modelResponse\"",
    "\"compiledModule\"",
    "\"shaderProgram\""
  };

  for (const std::string& field : disallowedFields) {
    GRAPPLE_REQUIRE(payload.find(field) == std::string::npos);
  }
}

} // namespace

int main() {
  using namespace grapple;

  agent::AgentToolRegistry registry;
  const auto registered = registry.registerTool(agent::makeProjectInspectTool());
  GRAPPLE_REQUIRE(registered);
  const auto registeredAssetList = registry.registerTool(agent::makeAssetListTool());
  GRAPPLE_REQUIRE(registeredAssetList);
  const auto registeredAssetImport = registry.registerTool(agent::makeAssetImportTool());
  GRAPPLE_REQUIRE(registeredAssetImport);
  const auto registeredCompositionInspect = registry.registerTool(agent::makeCompositionInspectTool());
  GRAPPLE_REQUIRE(registeredCompositionInspect);
  const auto registeredCreateTrack = registry.registerTool(agent::makeTimelineCreateTrackTool());
  GRAPPLE_REQUIRE(registeredCreateTrack);
  const auto registeredCreateClip = registry.registerTool(agent::makeTimelineCreateClipTool());
  GRAPPLE_REQUIRE(registeredCreateClip);
  const auto registeredMoveClip = registry.registerTool(agent::makeTimelineMoveClipTool());
  GRAPPLE_REQUIRE(registeredMoveClip);
  const auto registeredTrimClip = registry.registerTool(agent::makeTimelineTrimClipTool());
  GRAPPLE_REQUIRE(registeredTrimClip);
  const auto registeredCreateEffect = registry.registerTool(agent::makeEffectCreateNodeTool());
  GRAPPLE_REQUIRE(registeredCreateEffect);
  const auto registeredUpdateEffectParamValue = registry.registerTool(agent::makeEffectUpdateParamValueTool());
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValue);
  const auto registeredConnectPorts = registry.registerTool(agent::makeEffectConnectPortsTool());
  GRAPPLE_REQUIRE(registeredConnectPorts);
  const auto registeredDisconnectPorts = registry.registerTool(agent::makeEffectDisconnectPortsTool());
  GRAPPLE_REQUIRE(registeredDisconnectPorts);
  const auto registeredRenderPlanInspect = registry.registerTool(agent::makeRenderPlanInspectTool());
  GRAPPLE_REQUIRE(registeredRenderPlanInspect);
  const auto registeredRuntimeInspectDiagnostics = registry.registerTool(agent::makeRuntimeInspectDiagnosticsTool());
  GRAPPLE_REQUIRE(registeredRuntimeInspectDiagnostics);
  const auto registeredCreateNote = registry.registerTool(agent::makeNoteCreateTool());
  GRAPPLE_REQUIRE(registeredCreateNote);
  const auto registeredUpdateNote = registry.registerTool(agent::makeNoteUpdateTool());
  GRAPPLE_REQUIRE(registeredUpdateNote);
  GRAPPLE_REQUIRE(registry.tools().size() == 16);
  std::vector<std::string> serializedToolIds;
  serializedToolIds.reserve(registry.tools().size());
  for (const agent::AgentTool& tool : registry.tools()) {
    serializedToolIds.push_back(tool.serializedId);
  }
  GRAPPLE_REQUIRE(allUnique(std::move(serializedToolIds)));
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.inspect") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("asset.list") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("asset.import") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("composition.inspect") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.create_track") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.create_clip") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.move_clip") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.trim_clip") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.create_node") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.update_param_value") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.connect_ports") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.disconnect_ports") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("render_plan.inspect") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("runtime.inspect_diagnostics") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.create_effect") == nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("note.create") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("note.update") != nullptr);
  const agent::AgentTool* registeredAssetListTool = registry.findBySerializedId("asset.list");
  GRAPPLE_REQUIRE(registeredAssetListTool != nullptr);
  GRAPPLE_REQUIRE(registeredAssetListTool->schema.find("\"additionalProperties\": false") != std::string::npos);
  GRAPPLE_REQUIRE(registeredAssetListTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredAssetImportTool = registry.findBySerializedId("asset.import");
  GRAPPLE_REQUIRE(registeredAssetImportTool != nullptr);
  GRAPPLE_REQUIRE(registeredAssetImportTool->schema.find("\"assetId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredAssetImportTool->schema.find("\"mediaType\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredAssetImportTool->schema.find("\"sourcePath\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredAssetImportTool->schema.find("\"frameRate\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredAssetImportTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCompositionInspectTool = registry.findBySerializedId("composition.inspect");
  GRAPPLE_REQUIRE(registeredCompositionInspectTool != nullptr);
  GRAPPLE_REQUIRE(registeredCompositionInspectTool->schema.find("\"additionalProperties\": false") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCompositionInspectTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateTrackTool = registry.findBySerializedId("timeline.create_track");
  GRAPPLE_REQUIRE(registeredCreateTrackTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateTrackTool->schema.find("\"compositionNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateTrackTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateClipTool = registry.findBySerializedId("timeline.create_clip");
  GRAPPLE_REQUIRE(registeredCreateClipTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateClipTool->schema.find("\"trackNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateClipTool->schema.find("\"sourceRange\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateClipTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredMoveClipTool = registry.findBySerializedId("timeline.move_clip");
  GRAPPLE_REQUIRE(registeredMoveClipTool != nullptr);
  GRAPPLE_REQUIRE(registeredMoveClipTool->schema.find("\"clipNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredMoveClipTool->schema.find("\"newStart\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredMoveClipTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredTrimClipTool = registry.findBySerializedId("timeline.trim_clip");
  GRAPPLE_REQUIRE(registeredTrimClipTool != nullptr);
  GRAPPLE_REQUIRE(registeredTrimClipTool->schema.find("\"clipNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredTrimClipTool->schema.find("\"timelineRange\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredTrimClipTool->schema.find("\"sourceRange\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredTrimClipTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateEffectTool = registry.findBySerializedId("effect.create_node");
  GRAPPLE_REQUIRE(registeredCreateEffectTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"targetNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"params\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"minItems\": 1") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"numeric\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"boolean\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"commandId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"effectNodeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"targetEdgeId\"") == std::string::npos);
  const agent::AgentTool* registeredUpdateEffectParamValueTool = registry.findBySerializedId("effect.update_param_value");
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool != nullptr);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"effectNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"paramName\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"value\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"boolean\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"params\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"numeric\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredConnectPortsTool = registry.findBySerializedId("effect.connect_ports");
  GRAPPLE_REQUIRE(registeredConnectPortsTool != nullptr);
  GRAPPLE_REQUIRE(registeredConnectPortsTool->schema.find("\"edgeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredConnectPortsTool->schema.find("\"sourceNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredConnectPortsTool->schema.find("\"sourcePort\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredConnectPortsTool->schema.find("\"targetNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredConnectPortsTool->schema.find("\"targetPort\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredConnectPortsTool->schema.find("\"type\": \"integer\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredConnectPortsTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredDisconnectPortsTool = registry.findBySerializedId("effect.disconnect_ports");
  GRAPPLE_REQUIRE(registeredDisconnectPortsTool != nullptr);
  GRAPPLE_REQUIRE(registeredDisconnectPortsTool->schema.find("\"edgeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredDisconnectPortsTool->schema.find("\"sourceNodeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredDisconnectPortsTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredRenderPlanInspectTool = registry.findBySerializedId("render_plan.inspect");
  GRAPPLE_REQUIRE(registeredRenderPlanInspectTool != nullptr);
  GRAPPLE_REQUIRE(registeredRenderPlanInspectTool->schema.find("\"additionalProperties\": false") != std::string::npos);
  GRAPPLE_REQUIRE(registeredRenderPlanInspectTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredRuntimeInspectDiagnosticsTool = registry.findBySerializedId("runtime.inspect_diagnostics");
  GRAPPLE_REQUIRE(registeredRuntimeInspectDiagnosticsTool != nullptr);
  GRAPPLE_REQUIRE(registeredRuntimeInspectDiagnosticsTool->schema.find("\"additionalProperties\": false") != std::string::npos);
  GRAPPLE_REQUIRE(registeredRuntimeInspectDiagnosticsTool->schema.find("\"commandId\"") == std::string::npos);

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

  TestAgentCommandService commands{project};
  TestAgentQueryService queries{project};
  agent::AgentToolContext context{commands, queries};

  const agent::AgentTool* inspect = registry.findBySerializedId("project.inspect");
  GRAPPLE_REQUIRE(inspect != nullptr);

  const auto result = inspect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_project_inspect"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      command.value().afterRevision,
      "{}"
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
  GRAPPLE_REQUIRE(commands.applyCount() == 0);
  GRAPPLE_REQUIRE(queries.totalQueryCount() == 1);
  GRAPPLE_REQUIRE(queries.snapshotQueryCount() == 1);

  const auto inspectWithUnexpectedArgument = inspect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_project_inspect"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      command.value().afterRevision,
      R"({"unused": true})"
    },
    context
  );
  GRAPPLE_REQUIRE(!inspectWithUnexpectedArgument);
  GRAPPLE_REQUIRE(inspectWithUnexpectedArgument.error().code == "agent.tool_arguments_invalid");
  GRAPPLE_REQUIRE(inspectWithUnexpectedArgument.error().message.find("Unexpected tool argument") != std::string::npos);

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
  GRAPPLE_REQUIRE(commands.applyCount() == 0);

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
  GRAPPLE_REQUIRE(commands.applyCount() == 0);

  const auto effectWithUnexpectedNestedArgument = createEffect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_create_node"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      camera.value().afterRevision,
      R"({
        "targetNodeId": "node_camera",
        "displayName": "Extra Effect",
        "implementationKind": "python",
        "language": "python",
        "entrypoint": "prepare",
        "source": "def prepare(ctx):\n  return {}\n",
        "sourcePort": "camera_transform",
        "targetPort": "input",
        "inputPorts": ["frame"],
        "outputPorts": ["camera_transform"],
        "activeRange": {"start": 0, "end": 10, "unused": true},
        "params": [
          {
            "name": "target_x",
            "label": "Target X",
            "value": 0.5,
            "numeric": {"min": 0, "max": 1}
          }
        ]
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(!effectWithUnexpectedNestedArgument);
  GRAPPLE_REQUIRE(effectWithUnexpectedNestedArgument.error().code == "agent.tool_arguments_invalid");
  GRAPPLE_REQUIRE(effectWithUnexpectedNestedArgument.error().message.find("Unexpected tool argument") != std::string::npos);
  GRAPPLE_REQUIRE(commands.applyCount() == 0);

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
          },
          {
            "name": "lock_subject",
            "label": "Lock Subject",
            "value": true
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
  GRAPPLE_REQUIRE(effectPayload->params.values.size() == 2);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].name == "target_x");
  GRAPPLE_REQUIRE(std::get<double>(effectPayload->params.values[0].value) == 0.5);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.label == "Target X");
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric.has_value());
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric->min == 0.0);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric->max == 1.0);
  GRAPPLE_REQUIRE(effectPayload->params.values[0].control.numeric->step == 0.01);
  GRAPPLE_REQUIRE(effectPayload->params.values[1].name == "lock_subject");
  GRAPPLE_REQUIRE(std::get<bool>(effectPayload->params.values[1].value));
  GRAPPLE_REQUIRE(effectPayload->params.values[1].control.label == "Lock Subject");
  GRAPPLE_REQUIRE(!effectPayload->params.values[1].control.numeric.has_value());
  GRAPPLE_REQUIRE(effectPayload->activeRange.end == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(commands.applyCount() == 1);

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
  GRAPPLE_REQUIRE(commands.applyCount() == 4);

  const agent::AgentTool* importAsset = registry.findBySerializedId("asset.import");
  GRAPPLE_REQUIRE(importAsset != nullptr);
  const auto importAssetResult = importAsset->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_asset_import"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      updateNoteResult.value().observedRevision,
      R"({
        "assetId": "asset_video",
        "name": "Walking Woman",
        "mediaType": "video",
        "sourcePath": "/media/walking-woman.mov",
        "thumbnailPath": "/media/thumbs/walking-woman.jpg",
        "duration": 10,
        "dimensions": {"width": 1080, "height": 1920},
        "frameRate": {"numerator": 30000, "denominator": 1001}
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(importAssetResult);
  GRAPPLE_REQUIRE(importAssetResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(importAssetResult.value().observedRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(importAssetResult.value().payload == "{\"assetId\":\"asset_video\",\"revision\":\"rev_6\"}");

  const std::size_t commandCountBeforeAssetList = commands.applyCount();
  const std::size_t queryCountBeforeAssetList = queries.totalQueryCount();
  const agent::AgentTool* listAssets = registry.findBySerializedId("asset.list");
  GRAPPLE_REQUIRE(listAssets != nullptr);
  const auto listAssetsResult = listAssets->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_asset_list"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      importAssetResult.value().observedRevision,
      "{}"
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
  GRAPPLE_REQUIRE(commands.applyCount() == 5);
  GRAPPLE_REQUIRE(commands.applyCount() == commandCountBeforeAssetList);
  GRAPPLE_REQUIRE(queries.totalQueryCount() == queryCountBeforeAssetList + 1);

  const agent::AgentTool* createTrack = registry.findBySerializedId("timeline.create_track");
  GRAPPLE_REQUIRE(createTrack != nullptr);
  const auto createTrackResult = createTrack->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_create_track"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      importAssetResult.value().observedRevision,
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

  const agent::AgentTool* createClip = registry.findBySerializedId("timeline.create_clip");
  GRAPPLE_REQUIRE(createClip != nullptr);
  const auto createClipResult = createClip->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_create_clip"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      createTrackResult.value().observedRevision,
      R"({
        "trackNodeId": "node_agent_track_rev_7",
        "assetId": "asset_video",
        "kind": "video",
        "timelineRange": {"start": 0, "end": 4},
        "sourceRange": {"start": 1, "end": 5},
        "playbackRate": 1
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(createClipResult);
  GRAPPLE_REQUIRE(createClipResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(createClipResult.value().observedRevision == foundation::RevisionId{"rev_8"});
  GRAPPLE_REQUIRE(createClipResult.value().payload == "{\"commandId\":\"cmd_agent_create_clip_rev_8\",\"clipNodeId\":\"node_agent_clip_rev_8\",\"containmentEdgeId\":\"edge_agent_clip_contains_rev_8\",\"trackNodeId\":\"node_agent_track_rev_7\",\"assetId\":\"asset_video\",\"revision\":\"rev_8\"}");

  const auto afterClipSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterClipSnapshot);
  const graph::GraphNode* clipNode = afterClipSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_clip_rev_8"});
  GRAPPLE_REQUIRE(clipNode != nullptr);
  GRAPPLE_REQUIRE(clipNode->kind == graph::NodeKind::Clip);
  const auto* clipPayload = std::get_if<timeline::ClipPayload>(&clipNode->payload);
  GRAPPLE_REQUIRE(clipPayload != nullptr);
  GRAPPLE_REQUIRE(clipPayload->kind == timeline::ClipKind::Video);
  GRAPPLE_REQUIRE(clipPayload->assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(clipPayload->timelineRange.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(clipPayload->timelineRange.end == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(clipPayload->sourceRange.start == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(clipPayload->sourceRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(clipPayload->playbackRate == 1.0);
  const graph::GraphEdge* clipEdge = nullptr;
  for (const graph::GraphEdge& edge : afterClipSnapshot.value().graph.edges()) {
    if (edge.id == foundation::EdgeId{"edge_agent_clip_contains_rev_8"}) {
      clipEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(clipEdge != nullptr);
  GRAPPLE_REQUIRE(clipEdge->kind == graph::EdgeKind::Contains);
  GRAPPLE_REQUIRE(clipEdge->sourceNodeId == foundation::NodeId{"node_agent_track_rev_7"});
  GRAPPLE_REQUIRE(clipEdge->targetNodeId == foundation::NodeId{"node_agent_clip_rev_8"});

  const agent::AgentTool* moveClip = registry.findBySerializedId("timeline.move_clip");
  GRAPPLE_REQUIRE(moveClip != nullptr);
  const auto moveClipResult = moveClip->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_move_clip"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      createClipResult.value().observedRevision,
      R"({
        "clipNodeId": "node_agent_clip_rev_8",
        "newStart": 2
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(moveClipResult);
  GRAPPLE_REQUIRE(moveClipResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(moveClipResult.value().observedRevision == foundation::RevisionId{"rev_9"});
  GRAPPLE_REQUIRE(moveClipResult.value().payload == "{\"commandId\":\"cmd_agent_move_clip_rev_9\",\"clipNodeId\":\"node_agent_clip_rev_8\",\"newStart\":2,\"revision\":\"rev_9\"}");

  const auto afterMoveSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterMoveSnapshot);
  const graph::GraphNode* movedClipNode = afterMoveSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_clip_rev_8"});
  GRAPPLE_REQUIRE(movedClipNode != nullptr);
  const auto* movedClipPayload = std::get_if<timeline::ClipPayload>(&movedClipNode->payload);
  GRAPPLE_REQUIRE(movedClipPayload != nullptr);
  GRAPPLE_REQUIRE(movedClipPayload->timelineRange.start == foundation::TimeSeconds{2.0});
  GRAPPLE_REQUIRE(movedClipPayload->timelineRange.end == foundation::TimeSeconds{6.0});
  GRAPPLE_REQUIRE(movedClipPayload->sourceRange.start == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(movedClipPayload->sourceRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(movedClipPayload->assetId == foundation::AssetId{"asset_video"});

  const agent::AgentTool* trimClip = registry.findBySerializedId("timeline.trim_clip");
  GRAPPLE_REQUIRE(trimClip != nullptr);
  const auto trimClipResult = trimClip->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_trim_clip"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      moveClipResult.value().observedRevision,
      R"({
        "clipNodeId": "node_agent_clip_rev_8",
        "timelineRange": {"start": 3, "end": 5},
        "sourceRange": {"start": 2, "end": 4}
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(trimClipResult);
  GRAPPLE_REQUIRE(trimClipResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(trimClipResult.value().observedRevision == foundation::RevisionId{"rev_10"});
  GRAPPLE_REQUIRE(trimClipResult.value().payload == "{\"commandId\":\"cmd_agent_trim_clip_rev_10\",\"clipNodeId\":\"node_agent_clip_rev_8\",\"revision\":\"rev_10\"}");

  const auto afterTrimSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterTrimSnapshot);
  const graph::GraphNode* trimmedClipNode = afterTrimSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_clip_rev_8"});
  GRAPPLE_REQUIRE(trimmedClipNode != nullptr);
  const auto* trimmedClipPayload = std::get_if<timeline::ClipPayload>(&trimmedClipNode->payload);
  GRAPPLE_REQUIRE(trimmedClipPayload != nullptr);
  GRAPPLE_REQUIRE(trimmedClipPayload->timelineRange.start == foundation::TimeSeconds{3.0});
  GRAPPLE_REQUIRE(trimmedClipPayload->timelineRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(trimmedClipPayload->sourceRange.start == foundation::TimeSeconds{2.0});
  GRAPPLE_REQUIRE(trimmedClipPayload->sourceRange.end == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(trimmedClipPayload->assetId == foundation::AssetId{"asset_video"});

  const agent::AgentTool* updateEffectParamValue = registry.findBySerializedId("effect.update_param_value");
  GRAPPLE_REQUIRE(updateEffectParamValue != nullptr);
  const auto updateEffectParamValueResult = updateEffectParamValue->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_update_param_value"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      trimClipResult.value().observedRevision,
      R"({
        "effectNodeId": "node_agent_effect_rev_3",
        "paramName": "target_x",
        "value": 0.75
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(updateEffectParamValueResult);
  GRAPPLE_REQUIRE(updateEffectParamValueResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(updateEffectParamValueResult.value().observedRevision == foundation::RevisionId{"rev_11"});
  GRAPPLE_REQUIRE(updateEffectParamValueResult.value().payload == "{\"commandId\":\"cmd_agent_update_effect_param_value_rev_11\",\"effectNodeId\":\"node_agent_effect_rev_3\",\"paramName\":\"target_x\",\"revision\":\"rev_11\"}");

  const auto afterParamUpdateSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterParamUpdateSnapshot);
  const graph::GraphNode* updatedEffectNode = afterParamUpdateSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_effect_rev_3"});
  GRAPPLE_REQUIRE(updatedEffectNode != nullptr);
  const auto* updatedEffectPayload = std::get_if<timeline::EffectPayload>(&updatedEffectNode->payload);
  GRAPPLE_REQUIRE(updatedEffectPayload != nullptr);
  GRAPPLE_REQUIRE(updatedEffectPayload->params.values.size() == 2);
  GRAPPLE_REQUIRE(updatedEffectPayload->params.values[0].name == "target_x");
  GRAPPLE_REQUIRE(std::get<double>(updatedEffectPayload->params.values[0].value) == 0.75);
  GRAPPLE_REQUIRE(updatedEffectPayload->params.values[0].control.label == "Target X");
  GRAPPLE_REQUIRE(updatedEffectPayload->params.values[0].control.numeric.has_value());
  GRAPPLE_REQUIRE(updatedEffectPayload->params.values[0].control.numeric->step == 0.01);
  GRAPPLE_REQUIRE(updatedEffectPayload->params.values[1].name == "lock_subject");
  GRAPPLE_REQUIRE(std::get<bool>(updatedEffectPayload->params.values[1].value));

  const agent::AgentTool* connectPorts = registry.findBySerializedId("effect.connect_ports");
  GRAPPLE_REQUIRE(connectPorts != nullptr);
  const auto connectPortsResult = connectPorts->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_connect_ports"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      updateEffectParamValueResult.value().observedRevision,
      R"({
        "edgeId": "edge_agent_effect_ports",
        "sourceNodeId": "node_agent_effect_rev_3",
        "sourcePort": "camera_transform",
        "targetNodeId": "node_camera",
        "targetPort": "camera_transform_input",
        "order": 4
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(connectPortsResult);
  GRAPPLE_REQUIRE(connectPortsResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(connectPortsResult.value().observedRevision == foundation::RevisionId{"rev_12"});
  GRAPPLE_REQUIRE(connectPortsResult.value().payload == "{\"edgeId\":\"edge_agent_effect_ports\",\"revision\":\"rev_12\"}");

  const auto afterConnectPortsSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterConnectPortsSnapshot);
  const graph::GraphEdge* connectedPortsEdge = nullptr;
  for (const graph::GraphEdge& edge : afterConnectPortsSnapshot.value().graph.edges()) {
    if (edge.id == foundation::EdgeId{"edge_agent_effect_ports"}) {
      connectedPortsEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(connectedPortsEdge != nullptr);
  GRAPPLE_REQUIRE(connectedPortsEdge->kind == graph::EdgeKind::Connects);
  GRAPPLE_REQUIRE(connectedPortsEdge->sourceNodeId == foundation::NodeId{"node_agent_effect_rev_3"});
  GRAPPLE_REQUIRE(connectedPortsEdge->sourcePort == graph::PortName{"camera_transform"});
  GRAPPLE_REQUIRE(connectedPortsEdge->targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(connectedPortsEdge->targetPort == graph::PortName{"camera_transform_input"});
  GRAPPLE_REQUIRE(connectedPortsEdge->order == 4);

  const agent::AgentTool* disconnectPorts = registry.findBySerializedId("effect.disconnect_ports");
  GRAPPLE_REQUIRE(disconnectPorts != nullptr);
  const auto disconnectPortsResult = disconnectPorts->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_disconnect_ports"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      connectPortsResult.value().observedRevision,
      R"({
        "edgeId": "edge_agent_effect_ports"
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(disconnectPortsResult);
  GRAPPLE_REQUIRE(disconnectPortsResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(disconnectPortsResult.value().observedRevision == foundation::RevisionId{"rev_13"});
  GRAPPLE_REQUIRE(disconnectPortsResult.value().payload == "{\"edgeId\":\"edge_agent_effect_ports\",\"revision\":\"rev_13\"}");

  const auto afterDisconnectPortsSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterDisconnectPortsSnapshot);
  for (const graph::GraphEdge& edge : afterDisconnectPortsSnapshot.value().graph.edges()) {
    GRAPPLE_REQUIRE(edge.id != foundation::EdgeId{"edge_agent_effect_ports"});
  }
  GRAPPLE_REQUIRE(commands.applyCount() == 12);
  const std::size_t commandCountBeforeInspectTools = commands.applyCount();
  const std::size_t queryCountBeforeInspectTools = queries.totalQueryCount();

  const agent::AgentTool* inspectComposition = registry.findBySerializedId("composition.inspect");
  GRAPPLE_REQUIRE(inspectComposition != nullptr);
  const auto inspectCompositionResult = inspectComposition->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_composition_inspect"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      disconnectPortsResult.value().observedRevision,
      "{}"
    },
    context
  );
  GRAPPLE_REQUIRE(inspectCompositionResult);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().observedRevision == foundation::RevisionId{"rev_13"});
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"revision\":\"rev_13\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"nodeId\":\"node_composition\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"tracks\":[{\"nodeId\":\"node_agent_track_rev_7\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"clips\":[{\"nodeId\":\"node_agent_clip_rev_8\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"cameras\":[{\"nodeId\":\"node_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"effects\":[{\"nodeId\":\"node_agent_effect_rev_3\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"targetNodeId\":\"node_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"commandId\"") == std::string::npos);

  const agent::AgentTool* inspectRenderPlan = registry.findBySerializedId("render_plan.inspect");
  GRAPPLE_REQUIRE(inspectRenderPlan != nullptr);
  const auto inspectRenderPlanResult = inspectRenderPlan->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_render_plan_inspect"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      disconnectPortsResult.value().observedRevision,
      "{}"
    },
    context
  );
  GRAPPLE_REQUIRE(inspectRenderPlanResult);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().observedRevision == foundation::RevisionId{"rev_13"});
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"projectId\":\"proj_agent\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"revision\":\"rev_13\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"assetCount\":1") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"layers\":[{\"nodeId\":\"node_agent_track_rev_7\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"clips\":[{\"nodeId\":\"node_agent_clip_rev_8\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"cameras\":[{\"nodeId\":\"node_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"effectGraphs\":[{\"graphId\":\"effect_graph_node_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"diagnosticCount\":0") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"commandId\"") == std::string::npos);
  requireNoRuntimeOwnedFields(inspectRenderPlanResult.value().payload);

  const agent::AgentTool* inspectRuntimeDiagnostics = registry.findBySerializedId("runtime.inspect_diagnostics");
  GRAPPLE_REQUIRE(inspectRuntimeDiagnostics != nullptr);
  const auto inspectRuntimeDiagnosticsResult = inspectRuntimeDiagnostics->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_runtime_inspect_diagnostics"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      disconnectPortsResult.value().observedRevision,
      "{}"
    },
    context
  );
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult);
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().observedRevision == foundation::RevisionId{"rev_13"});
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().payload.find("\"revision\":\"rev_13\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().payload.find("\"code\":\"runtime.effect_runtime_missing\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().payload.find("\"severity\":\"warning\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().payload.find("\"nodeId\":\"node_agent_effect_rev_3\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().payload.find("\"commandId\"") == std::string::npos);
  requireNoRuntimeOwnedFields(inspectRuntimeDiagnosticsResult.value().payload);
  GRAPPLE_REQUIRE(commands.applyCount() == commandCountBeforeInspectTools);
  GRAPPLE_REQUIRE(queries.totalQueryCount() == queryCountBeforeInspectTools + 3);
  GRAPPLE_REQUIRE(queries.compositionQueryCount() == 1);
  GRAPPLE_REQUIRE(queries.renderPlanQueryCount() == 1);
  GRAPPLE_REQUIRE(queries.runtimeDiagnosticsQueryCount() == 1);

  return 0;
}
