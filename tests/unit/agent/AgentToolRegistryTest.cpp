#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
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
                  grapple::foundation::NodeId{"node_agent_track_3"},
                  "Agent Track"
                }
              },
              {},
              {
                grapple::project::RenderPlanClipSummary{
                  grapple::foundation::NodeId{"node_agent_clip_4"},
                  grapple::foundation::NodeId{"node_agent_track_3"},
                  grapple::foundation::AssetId{"asset_video"},
                  grapple::timeline::ClipKind::Video,
                  grapple::foundation::TimeRange{
                    grapple::foundation::TimeSeconds{3.0},
                    grapple::foundation::TimeSeconds{5.0}
                  }
                }
              },
              {},
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
                  grapple::foundation::NodeId{"node_agent_effect_1"},
                  "No runtime registered for this effect implementation."
                }
              }
            }
          };
        } else if constexpr (std::is_same_v<Query, grapple::project::GetProjectSnapshotQuery>) {
          ++snapshotQueryCount_;
          return project_.query(typedQuery);
        } else if constexpr (std::is_same_v<Query, grapple::project::GetAssetCatalogQuery>) {
          ++assetCatalogQueryCount_;
          return project_.query(typedQuery);
        } else if constexpr (std::is_same_v<Query, grapple::project::InspectCompositionsQuery>) {
          ++compositionQueryCount_;
          return project_.query(typedQuery);
        } else if constexpr (std::is_same_v<Query, grapple::project::ListNotesQuery>) {
          ++noteListQueryCount_;
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

  [[nodiscard]] std::size_t assetCatalogQueryCount() const noexcept {
    return assetCatalogQueryCount_;
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

  [[nodiscard]] std::size_t noteListQueryCount() const noexcept {
    return noteListQueryCount_;
  }

private:
  const grapple::project::ProjectController& project_;
  mutable std::size_t totalQueryCount_ = 0;
  mutable std::size_t snapshotQueryCount_ = 0;
  mutable std::size_t assetCatalogQueryCount_ = 0;
  mutable std::size_t compositionQueryCount_ = 0;
  mutable std::size_t renderPlanQueryCount_ = 0;
  mutable std::size_t runtimeDiagnosticsQueryCount_ = 0;
  mutable std::size_t noteListQueryCount_ = 0;
};

class TestProjectIdAllocator final : public grapple::project::IProjectIdAllocator {
public:
  grapple::foundation::CommandId nextCommandId() override {
    return grapple::foundation::CommandId{"cmd_agent_" + std::to_string(commandSequence_++)};
  }

  grapple::foundation::AssetId nextAssetId(const std::string& stem) override {
    return grapple::foundation::AssetId{"asset_agent_" + stem + "_" + std::to_string(assetSequence_++)};
  }

  grapple::foundation::NodeId nextNodeId(const std::string& stem) override {
    return grapple::foundation::NodeId{"node_agent_" + stem + "_" + std::to_string(nodeSequence_++)};
  }

  grapple::foundation::EdgeId nextEdgeId(const std::string& stem) override {
    return grapple::foundation::EdgeId{"edge_agent_" + stem + "_" + std::to_string(edgeSequence_++)};
  }

  grapple::foundation::KeyframeId nextKeyframeId(const std::string& stem) override {
    return grapple::foundation::KeyframeId{"key_agent_" + stem + "_" + std::to_string(keyframeSequence_++)};
  }

private:
  int commandSequence_ = 1;
  int assetSequence_ = 1;
  int nodeSequence_ = 1;
  int edgeSequence_ = 1;
  int keyframeSequence_ = 1;
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
  const auto registered = agent::registerProjectTools(registry);
  GRAPPLE_REQUIRE(registered);
  GRAPPLE_REQUIRE(registry.tools().size() == 26);
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
  GRAPPLE_REQUIRE(registry.findBySerializedId("camera.create") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("camera.update") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.create_track") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.delete_track") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.create_clip") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.delete_clip") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.move_clip") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.trim_clip") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("timeline.update_clip_transform") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.create_node") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.delete_node") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.update_param_value") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.create_param_keyframe") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.update_param_keyframe") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.delete_param_keyframe") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.connect_ports") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("effect.disconnect_ports") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("render_plan.inspect") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("runtime.inspect_diagnostics") != nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("project.create_effect") == nullptr);
  GRAPPLE_REQUIRE(registry.findBySerializedId("note.list") != nullptr);
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
  GRAPPLE_REQUIRE(registeredAssetImportTool->schema.find("\"minLength\": 1") != std::string::npos);
  GRAPPLE_REQUIRE(registeredAssetImportTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCompositionInspectTool = registry.findBySerializedId("composition.inspect");
  GRAPPLE_REQUIRE(registeredCompositionInspectTool != nullptr);
  GRAPPLE_REQUIRE(registeredCompositionInspectTool->schema.find("\"additionalProperties\": false") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCompositionInspectTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateCameraTool = registry.findBySerializedId("camera.create");
  GRAPPLE_REQUIRE(registeredCreateCameraTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateCameraTool->schema.find("\"compositionNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateCameraTool->schema.find("\"focalLength\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateCameraTool->schema.find("\"commandId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateCameraTool->schema.find("\"cameraNodeId\"") == std::string::npos);
  const agent::AgentTool* registeredUpdateCameraTool = registry.findBySerializedId("camera.update");
  GRAPPLE_REQUIRE(registeredUpdateCameraTool != nullptr);
  GRAPPLE_REQUIRE(registeredUpdateCameraTool->schema.find("\"cameraNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateCameraTool->schema.find("\"focalLength\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateCameraTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateTrackTool = registry.findBySerializedId("timeline.create_track");
  GRAPPLE_REQUIRE(registeredCreateTrackTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateTrackTool->schema.find("\"compositionNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateTrackTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredDeleteTrackTool = registry.findBySerializedId("timeline.delete_track");
  GRAPPLE_REQUIRE(registeredDeleteTrackTool != nullptr);
  GRAPPLE_REQUIRE(registeredDeleteTrackTool->schema.find("\"trackNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteTrackTool->schema.find("\"clipNodeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteTrackTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateClipTool = registry.findBySerializedId("timeline.create_clip");
  GRAPPLE_REQUIRE(registeredCreateClipTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateClipTool->schema.find("\"trackNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateClipTool->schema.find("\"sourceRange\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateClipTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredDeleteClipTool = registry.findBySerializedId("timeline.delete_clip");
  GRAPPLE_REQUIRE(registeredDeleteClipTool != nullptr);
  GRAPPLE_REQUIRE(registeredDeleteClipTool->schema.find("\"clipNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteClipTool->schema.find("\"trackNodeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteClipTool->schema.find("\"commandId\"") == std::string::npos);
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
  const agent::AgentTool* registeredUpdateClipTransformTool = registry.findBySerializedId("timeline.update_clip_transform");
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool != nullptr);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"clipNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"position\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"scale\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"rotationDegrees\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"opacity\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"assetId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"timelineRange\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateClipTransformTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateEffectTool = registry.findBySerializedId("effect.create_node");
  GRAPPLE_REQUIRE(registeredCreateEffectTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"targetNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"params\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"minItems\": 1") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"minLength\": 1") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"numeric\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"boolean\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"commandId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"effectNodeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectTool->schema.find("\"targetEdgeId\"") == std::string::npos);
  const agent::AgentTool* registeredDeleteEffectTool = registry.findBySerializedId("effect.delete_node");
  GRAPPLE_REQUIRE(registeredDeleteEffectTool != nullptr);
  GRAPPLE_REQUIRE(registeredDeleteEffectTool->schema.find("\"effectNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteEffectTool->schema.find("\"targetNodeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteEffectTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredUpdateEffectParamValueTool = registry.findBySerializedId("effect.update_param_value");
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool != nullptr);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"effectNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"paramName\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"value\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"boolean\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"params\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"numeric\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectParamValueTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredCreateEffectKeyframeTool = registry.findBySerializedId("effect.create_param_keyframe");
  GRAPPLE_REQUIRE(registeredCreateEffectKeyframeTool != nullptr);
  GRAPPLE_REQUIRE(registeredCreateEffectKeyframeTool->schema.find("\"effectNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectKeyframeTool->schema.find("\"paramName\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectKeyframeTool->schema.find("\"time\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectKeyframeTool->schema.find("\"value\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectKeyframeTool->schema.find("\"keyframeId\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredCreateEffectKeyframeTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredUpdateEffectKeyframeTool = registry.findBySerializedId("effect.update_param_keyframe");
  GRAPPLE_REQUIRE(registeredUpdateEffectKeyframeTool != nullptr);
  GRAPPLE_REQUIRE(registeredUpdateEffectKeyframeTool->schema.find("\"effectNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectKeyframeTool->schema.find("\"keyframeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectKeyframeTool->schema.find("\"time\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredUpdateEffectKeyframeTool->schema.find("\"commandId\"") == std::string::npos);
  const agent::AgentTool* registeredDeleteEffectKeyframeTool = registry.findBySerializedId("effect.delete_param_keyframe");
  GRAPPLE_REQUIRE(registeredDeleteEffectKeyframeTool != nullptr);
  GRAPPLE_REQUIRE(registeredDeleteEffectKeyframeTool->schema.find("\"effectNodeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteEffectKeyframeTool->schema.find("\"paramName\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteEffectKeyframeTool->schema.find("\"keyframeId\"") != std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteEffectKeyframeTool->schema.find("\"time\"") == std::string::npos);
  GRAPPLE_REQUIRE(registeredDeleteEffectKeyframeTool->schema.find("\"commandId\"") == std::string::npos);
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
  const agent::AgentTool* registeredNoteListTool = registry.findBySerializedId("note.list");
  GRAPPLE_REQUIRE(registeredNoteListTool != nullptr);
  GRAPPLE_REQUIRE(registeredNoteListTool->schema.find("\"additionalProperties\": false") != std::string::npos);
  GRAPPLE_REQUIRE(registeredNoteListTool->schema.find("\"commandId\"") == std::string::npos);

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
  TestProjectIdAllocator ids;
  agent::AgentToolContext context{commands, queries, ids};

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
      timeline::CameraPayload{"Camera", timeline::Transform2D{}, timeline::CameraLens{35.0}}
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

  const auto effectWithEmptyPortResult = createEffect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_create_node"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      camera.value().afterRevision,
      R"({
        "targetNodeId": "node_camera",
        "displayName": "Empty Port Effect",
        "implementationKind": "python",
        "language": "python",
        "entrypoint": "prepare",
        "source": "def prepare(ctx):\n  return {}\n",
        "sourcePort": "",
        "targetPort": "input",
        "inputPorts": ["frame"],
        "outputPorts": ["camera_transform"],
        "activeRange": {"start": 0, "end": 10},
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
  GRAPPLE_REQUIRE(!effectWithEmptyPortResult);
  GRAPPLE_REQUIRE(effectWithEmptyPortResult.error().code == "agent.tool_arguments_invalid");
  GRAPPLE_REQUIRE(effectWithEmptyPortResult.error().message.find("$.sourcePort") != std::string::npos);
  GRAPPLE_REQUIRE(effectWithEmptyPortResult.error().message.find("non-empty") != std::string::npos);
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
  GRAPPLE_REQUIRE(createEffectResult.value().payload == "{\"commandId\":\"cmd_agent_1\",\"effectNodeId\":\"node_agent_effect_1\",\"targetEdgeId\":\"edge_agent_effect_targets_1\",\"targetNodeId\":\"node_camera\",\"revision\":\"rev_3\"}");

  const auto afterEffectSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterEffectSnapshot);
  const graph::GraphNode* effectNode = afterEffectSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_effect_1"});
  GRAPPLE_REQUIRE(effectNode != nullptr);
  GRAPPLE_REQUIRE(effectNode->kind == graph::NodeKind::Effect);
  const graph::GraphEdge* effectEdge = nullptr;
  for (const graph::GraphEdge& edge : afterEffectSnapshot.value().graph.edges()) {
    if (edge.id == foundation::EdgeId{"edge_agent_effect_targets_1"}) {
      effectEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(effectEdge != nullptr);
  GRAPPLE_REQUIRE(effectEdge->sourceNodeId == foundation::NodeId{"node_agent_effect_1"});
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
  GRAPPLE_REQUIRE(createNoteResult.value().payload == "{\"commandId\":\"cmd_agent_2\",\"noteNodeId\":\"node_agent_note_2\",\"revision\":\"rev_4\"}");

  const agent::AgentTool* updateNote = registry.findBySerializedId("note.update");
  GRAPPLE_REQUIRE(updateNote != nullptr);
  const auto updateNoteResult = updateNote->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_note_update"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      createNoteResult.value().observedRevision,
      R"({
        "nodeId": "node_agent_note_2",
        "title": "Updated rationale",
        "markdown": "Parameter remains editable after creation."
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(updateNoteResult);
  GRAPPLE_REQUIRE(updateNoteResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(updateNoteResult.value().observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(updateNoteResult.value().payload == "{\"commandId\":\"cmd_agent_3\",\"noteNodeId\":\"node_agent_note_2\",\"revision\":\"rev_5\"}");
  const auto afterNoteSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterNoteSnapshot);
  const graph::GraphNode* noteNode = afterNoteSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_note_2"});
  GRAPPLE_REQUIRE(noteNode != nullptr);
  GRAPPLE_REQUIRE(noteNode->kind == graph::NodeKind::Note);
  const auto* notePayload = std::get_if<timeline::NotePayload>(&noteNode->payload);
  GRAPPLE_REQUIRE(notePayload != nullptr);
  GRAPPLE_REQUIRE(notePayload->title == "Updated rationale");
  GRAPPLE_REQUIRE(notePayload->markdown == "Parameter remains editable after creation.");

  const std::size_t commandCountBeforeNoteList = commands.applyCount();
  const std::size_t queryCountBeforeNoteList = queries.totalQueryCount();
  const agent::AgentTool* listNotes = registry.findBySerializedId("note.list");
  GRAPPLE_REQUIRE(listNotes != nullptr);
  const auto listNotesResult = listNotes->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_note_list"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      updateNoteResult.value().observedRevision,
      "{}"
    },
    context
  );
  GRAPPLE_REQUIRE(listNotesResult);
  GRAPPLE_REQUIRE(listNotesResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(listNotesResult.value().observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(
    listNotesResult.value().payload ==
    "{\"revision\":\"rev_5\",\"notes\":[{\"nodeId\":\"node_agent_note_2\",\"title\":\"Updated rationale\",\"markdown\":\"Parameter remains editable after creation.\",\"enabled\":true}]}"
  );
  GRAPPLE_REQUIRE(commands.applyCount() == commandCountBeforeNoteList);
  GRAPPLE_REQUIRE(queries.totalQueryCount() == queryCountBeforeNoteList + 1);
  GRAPPLE_REQUIRE(queries.noteListQueryCount() == 1);

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
  GRAPPLE_REQUIRE(queries.assetCatalogQueryCount() == 1);

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
      "name": "Agent Track",
      "kind": "visual"
    })"
    },
    context
  );
  GRAPPLE_REQUIRE(createTrackResult);
  GRAPPLE_REQUIRE(createTrackResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(createTrackResult.value().observedRevision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(createTrackResult.value().payload == "{\"commandId\":\"cmd_agent_6\",\"trackNodeId\":\"node_agent_track_3\",\"containmentEdgeId\":\"edge_agent_contains_track_2\",\"compositionNodeId\":\"node_composition\",\"revision\":\"rev_7\"}");

  const auto afterTrackSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterTrackSnapshot);
  const graph::GraphNode* trackNode = afterTrackSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_track_3"});
  GRAPPLE_REQUIRE(trackNode != nullptr);
  GRAPPLE_REQUIRE(trackNode->kind == graph::NodeKind::Track);
  const auto* trackPayload = std::get_if<timeline::TrackPayload>(&trackNode->payload);
  GRAPPLE_REQUIRE(trackPayload != nullptr);
  GRAPPLE_REQUIRE(trackPayload->name == "Agent Track");
  GRAPPLE_REQUIRE(trackPayload->kind == timeline::TrackKind::Visual);
  const graph::GraphEdge* trackEdge = nullptr;
  for (const graph::GraphEdge& edge : afterTrackSnapshot.value().graph.edges()) {
    if (edge.id == foundation::EdgeId{"edge_agent_contains_track_2"}) {
      trackEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(trackEdge != nullptr);
  GRAPPLE_REQUIRE(trackEdge->kind == graph::EdgeKind::Contains);
  GRAPPLE_REQUIRE(trackEdge->sourceNodeId == foundation::NodeId{"node_composition"});
  GRAPPLE_REQUIRE(trackEdge->targetNodeId == foundation::NodeId{"node_agent_track_3"});

  const agent::AgentTool* createClip = registry.findBySerializedId("timeline.create_clip");
  GRAPPLE_REQUIRE(createClip != nullptr);
  const auto createClipResult = createClip->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_create_clip"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      createTrackResult.value().observedRevision,
      R"({
        "trackNodeId": "node_agent_track_3",
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
  GRAPPLE_REQUIRE(createClipResult.value().payload == "{\"commandId\":\"cmd_agent_7\",\"clipNodeId\":\"node_agent_clip_4\",\"containmentEdgeId\":\"edge_agent_contains_clip_3\",\"trackNodeId\":\"node_agent_track_3\",\"assetId\":\"asset_video\",\"revision\":\"rev_8\"}");

  const auto afterClipSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterClipSnapshot);
  const graph::GraphNode* clipNode = afterClipSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_clip_4"});
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
    if (edge.id == foundation::EdgeId{"edge_agent_contains_clip_3"}) {
      clipEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(clipEdge != nullptr);
  GRAPPLE_REQUIRE(clipEdge->kind == graph::EdgeKind::Contains);
  GRAPPLE_REQUIRE(clipEdge->sourceNodeId == foundation::NodeId{"node_agent_track_3"});
  GRAPPLE_REQUIRE(clipEdge->targetNodeId == foundation::NodeId{"node_agent_clip_4"});

  const agent::AgentTool* moveClip = registry.findBySerializedId("timeline.move_clip");
  GRAPPLE_REQUIRE(moveClip != nullptr);
  const auto moveClipResult = moveClip->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_move_clip"},
      foundation::RunId{"run_1"},
      foundation::ProjectId{"proj_agent"},
      createClipResult.value().observedRevision,
      R"({
        "clipNodeId": "node_agent_clip_4",
        "newStart": 2
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(moveClipResult);
  GRAPPLE_REQUIRE(moveClipResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(moveClipResult.value().observedRevision == foundation::RevisionId{"rev_9"});
  GRAPPLE_REQUIRE(moveClipResult.value().payload == "{\"commandId\":\"cmd_agent_8\",\"clipNodeId\":\"node_agent_clip_4\",\"newStart\":2,\"revision\":\"rev_9\"}");

  const auto afterMoveSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterMoveSnapshot);
  const graph::GraphNode* movedClipNode = afterMoveSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_clip_4"});
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
        "clipNodeId": "node_agent_clip_4",
        "timelineRange": {"start": 3, "end": 5},
        "sourceRange": {"start": 2, "end": 4}
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(trimClipResult);
  GRAPPLE_REQUIRE(trimClipResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(trimClipResult.value().observedRevision == foundation::RevisionId{"rev_10"});
  GRAPPLE_REQUIRE(trimClipResult.value().payload == "{\"commandId\":\"cmd_agent_9\",\"clipNodeId\":\"node_agent_clip_4\",\"revision\":\"rev_10\"}");

  const auto afterTrimSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterTrimSnapshot);
  const graph::GraphNode* trimmedClipNode = afterTrimSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_clip_4"});
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
        "effectNodeId": "node_agent_effect_1",
        "paramName": "target_x",
        "value": 0.75
      })"
    },
    context
  );
  GRAPPLE_REQUIRE(updateEffectParamValueResult);
  GRAPPLE_REQUIRE(updateEffectParamValueResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(updateEffectParamValueResult.value().observedRevision == foundation::RevisionId{"rev_11"});
  GRAPPLE_REQUIRE(updateEffectParamValueResult.value().payload == "{\"commandId\":\"cmd_agent_10\",\"effectNodeId\":\"node_agent_effect_1\",\"paramName\":\"target_x\",\"revision\":\"rev_11\"}");

  const auto afterParamUpdateSnapshot = project.snapshot();
  GRAPPLE_REQUIRE(afterParamUpdateSnapshot);
  const graph::GraphNode* updatedEffectNode = afterParamUpdateSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_effect_1"});
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
        "sourceNodeId": "node_agent_effect_1",
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
  GRAPPLE_REQUIRE(connectedPortsEdge->sourceNodeId == foundation::NodeId{"node_agent_effect_1"});
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
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"tracks\":[{\"nodeId\":\"node_agent_track_3\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"clips\":[{\"nodeId\":\"node_agent_clip_4\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"cameras\":[{\"nodeId\":\"node_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectCompositionResult.value().payload.find("\"effects\":[{\"nodeId\":\"node_agent_effect_1\"") != std::string::npos);
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
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"layers\":[{\"nodeId\":\"node_agent_track_3\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRenderPlanResult.value().payload.find("\"clips\":[{\"nodeId\":\"node_agent_clip_4\"") != std::string::npos);
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
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().payload.find("\"nodeId\":\"node_agent_effect_1\"") != std::string::npos);
  GRAPPLE_REQUIRE(inspectRuntimeDiagnosticsResult.value().payload.find("\"commandId\"") == std::string::npos);
  requireNoRuntimeOwnedFields(inspectRuntimeDiagnosticsResult.value().payload);
  GRAPPLE_REQUIRE(commands.applyCount() == commandCountBeforeInspectTools);
  GRAPPLE_REQUIRE(queries.totalQueryCount() == queryCountBeforeInspectTools + 3);
  GRAPPLE_REQUIRE(queries.compositionQueryCount() == 1);
  GRAPPLE_REQUIRE(queries.renderPlanQueryCount() == 1);
  GRAPPLE_REQUIRE(queries.runtimeDiagnosticsQueryCount() == 1);

  project::ProjectController cameraProject{
    project::createEmptyProject(foundation::ProjectId{"proj_agent_camera"}, "Agent Camera Project")
  };
  const auto cameraInitial = cameraProject.snapshot();
  GRAPPLE_REQUIRE(cameraInitial);
  const auto cameraComposition = cameraProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_camera_composition"},
    foundation::ProjectId{"proj_agent_camera"},
    cameraInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_camera"}, "agent"},
    project::CreateCompositionCommand{foundation::NodeId{"node_camera_composition"}, "Camera Main"}
  });
  GRAPPLE_REQUIRE(cameraComposition);

  TestAgentCommandService cameraCommands{cameraProject};
  TestAgentQueryService cameraQueries{cameraProject};
  TestProjectIdAllocator cameraIds;
  agent::AgentToolContext cameraContext{cameraCommands, cameraQueries, cameraIds};

  const agent::AgentTool* createCamera = registry.findBySerializedId("camera.create");
  GRAPPLE_REQUIRE(createCamera != nullptr);
  const auto createCameraResult = createCamera->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_camera_create"},
      foundation::RunId{"run_camera"},
      foundation::ProjectId{"proj_agent_camera"},
      cameraComposition.value().afterRevision,
      R"({
        "compositionNodeId": "node_camera_composition",
        "name": "Agent Camera",
        "focalLength": 50
      })"
    },
    cameraContext
  );
  GRAPPLE_REQUIRE(createCameraResult);
  GRAPPLE_REQUIRE(createCameraResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(createCameraResult.value().observedRevision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(createCameraResult.value().payload == "{\"commandId\":\"cmd_agent_1\",\"cameraNodeId\":\"node_agent_camera_1\",\"containmentEdgeId\":\"edge_agent_contains_camera_1\",\"compositionNodeId\":\"node_camera_composition\",\"revision\":\"rev_2\"}");

  const auto afterCreateCameraSnapshot = cameraProject.snapshot();
  GRAPPLE_REQUIRE(afterCreateCameraSnapshot);
  const graph::GraphNode* createdCameraNode = afterCreateCameraSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_camera_1"});
  GRAPPLE_REQUIRE(createdCameraNode != nullptr);
  GRAPPLE_REQUIRE(createdCameraNode->kind == graph::NodeKind::Camera);
  const auto* createdCameraPayload = std::get_if<timeline::CameraPayload>(&createdCameraNode->payload);
  GRAPPLE_REQUIRE(createdCameraPayload != nullptr);
  GRAPPLE_REQUIRE(createdCameraPayload->name == "Agent Camera");
  GRAPPLE_REQUIRE(createdCameraPayload->lens.focalLength == 50.0);

  const graph::GraphEdge* createdCameraEdge = nullptr;
  for (const graph::GraphEdge& edge : afterCreateCameraSnapshot.value().graph.edges()) {
    if (edge.id == foundation::EdgeId{"edge_agent_contains_camera_1"}) {
      createdCameraEdge = &edge;
      break;
    }
  }
  GRAPPLE_REQUIRE(createdCameraEdge != nullptr);
  GRAPPLE_REQUIRE(createdCameraEdge->kind == graph::EdgeKind::Contains);
  GRAPPLE_REQUIRE(createdCameraEdge->sourceNodeId == foundation::NodeId{"node_camera_composition"});
  GRAPPLE_REQUIRE(createdCameraEdge->targetNodeId == foundation::NodeId{"node_agent_camera_1"});

  const agent::AgentTool* updateCamera = registry.findBySerializedId("camera.update");
  GRAPPLE_REQUIRE(updateCamera != nullptr);
  const auto updateCameraResult = updateCamera->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_camera_update"},
      foundation::RunId{"run_camera"},
      foundation::ProjectId{"proj_agent_camera"},
      createCameraResult.value().observedRevision,
      R"({
        "cameraNodeId": "node_agent_camera_1",
        "name": "Renamed Agent Camera",
        "focalLength": 85
      })"
    },
    cameraContext
  );
  GRAPPLE_REQUIRE(updateCameraResult);
  GRAPPLE_REQUIRE(updateCameraResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(updateCameraResult.value().observedRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(updateCameraResult.value().payload == "{\"commandId\":\"cmd_agent_2\",\"cameraNodeId\":\"node_agent_camera_1\",\"revision\":\"rev_3\"}");

  const auto afterUpdateCameraSnapshot = cameraProject.snapshot();
  GRAPPLE_REQUIRE(afterUpdateCameraSnapshot);
  const graph::GraphNode* updatedCameraNode = afterUpdateCameraSnapshot.value().graph.findNode(foundation::NodeId{"node_agent_camera_1"});
  GRAPPLE_REQUIRE(updatedCameraNode != nullptr);
  const auto* updatedCameraPayload = std::get_if<timeline::CameraPayload>(&updatedCameraNode->payload);
  GRAPPLE_REQUIRE(updatedCameraPayload != nullptr);
  GRAPPLE_REQUIRE(updatedCameraPayload->name == "Renamed Agent Camera");
  GRAPPLE_REQUIRE(updatedCameraPayload->lens.focalLength == 85.0);
  GRAPPLE_REQUIRE(updatedCameraPayload->transform == createdCameraPayload->transform);
  GRAPPLE_REQUIRE(cameraCommands.applyCount() == 2);
  GRAPPLE_REQUIRE(cameraQueries.snapshotQueryCount() == 1);

  project::ProjectController effectDeleteProject{
    project::createEmptyProject(foundation::ProjectId{"proj_agent_effect_delete"}, "Agent Effect Delete Project")
  };
  const auto effectDeleteInitial = effectDeleteProject.snapshot();
  GRAPPLE_REQUIRE(effectDeleteInitial);
  const auto effectDeleteComposition = effectDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_delete_composition"},
    foundation::ProjectId{"proj_agent_effect_delete"},
    effectDeleteInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect_delete"}, "agent"},
    project::CreateCompositionCommand{foundation::NodeId{"node_effect_delete_composition"}, "Effect Delete Main"}
  });
  GRAPPLE_REQUIRE(effectDeleteComposition);
  const auto effectDeleteCamera = effectDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_delete_camera"},
    foundation::ProjectId{"proj_agent_effect_delete"},
    effectDeleteComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect_delete"}, "agent"},
    project::CreateCameraCommand{
      foundation::NodeId{"node_effect_delete_camera"},
      foundation::NodeId{"node_effect_delete_composition"},
      foundation::EdgeId{"edge_effect_delete_contains_camera"},
      timeline::CameraPayload{"Effect Delete Camera", timeline::Transform2D{}, timeline::CameraLens{35.0}}
    }
  });
  GRAPPLE_REQUIRE(effectDeleteCamera);
  const std::string effectDeleteSource = "def prepare(ctx):\n  return {}\n";
  const auto effectDeleteEffect = effectDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_delete_effect"},
    foundation::ProjectId{"proj_agent_effect_delete"},
    effectDeleteCamera.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect_delete"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_effect_delete_effect"},
      foundation::NodeId{"node_effect_delete_camera"},
      foundation::EdgeId{"edge_effect_delete_targets"},
      timeline::EffectPayload{
        "Delete Me",
        timeline::EffectImplementation{
          timeline::EffectImplementationKind::Python,
          "prepare",
          timeline::EffectSource{
            timeline::EffectSourceKind::InlineSource,
            "python",
            effectDeleteSource,
            std::nullopt,
            foundation::stableHash(effectDeleteSource)
          }
        },
        timeline::EffectPortSet{
          {timeline::EffectPort{"frame"}},
          {timeline::EffectPort{"camera_transform"}}
        },
        timeline::ParamSet{
          {
            timeline::Param{
              "strength",
              1.0,
              timeline::Param::Control{
                "Strength",
                timeline::Param::NumericControl{0.0, 1.0, 0.01}
              },
              {}
            }
          }
        },
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
        {}
      },
      graph::PortName{"camera_transform"},
      graph::PortName{"input"},
      0
    }
  });
  GRAPPLE_REQUIRE(effectDeleteEffect);

  TestAgentCommandService effectDeleteCommands{effectDeleteProject};
  TestAgentQueryService effectDeleteQueries{effectDeleteProject};
  TestProjectIdAllocator effectDeleteIds;
  agent::AgentToolContext effectDeleteContext{effectDeleteCommands, effectDeleteQueries, effectDeleteIds};

  const agent::AgentTool* deleteEffect = registry.findBySerializedId("effect.delete_node");
  GRAPPLE_REQUIRE(deleteEffect != nullptr);
  const auto deleteEffectResult = deleteEffect->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_delete_node"},
      foundation::RunId{"run_effect_delete"},
      foundation::ProjectId{"proj_agent_effect_delete"},
      effectDeleteEffect.value().afterRevision,
      R"({
        "effectNodeId": "node_effect_delete_effect"
      })"
    },
    effectDeleteContext
  );
  GRAPPLE_REQUIRE(deleteEffectResult);
  GRAPPLE_REQUIRE(deleteEffectResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(deleteEffectResult.value().observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(deleteEffectResult.value().payload == "{\"commandId\":\"cmd_agent_1\",\"effectNodeId\":\"node_effect_delete_effect\",\"revision\":\"rev_4\"}");

  const auto afterEffectDeleteSnapshot = effectDeleteProject.snapshot();
  GRAPPLE_REQUIRE(afterEffectDeleteSnapshot);
  GRAPPLE_REQUIRE(afterEffectDeleteSnapshot.value().graph.findNode(foundation::NodeId{"node_effect_delete_effect"}) == nullptr);
  for (const graph::GraphEdge& edge : afterEffectDeleteSnapshot.value().graph.edges()) {
    GRAPPLE_REQUIRE(edge.id != foundation::EdgeId{"edge_effect_delete_targets"});
    GRAPPLE_REQUIRE(edge.sourceNodeId != foundation::NodeId{"node_effect_delete_effect"});
    GRAPPLE_REQUIRE(edge.targetNodeId != foundation::NodeId{"node_effect_delete_effect"});
  }
  GRAPPLE_REQUIRE(effectDeleteCommands.applyCount() == 1);
  GRAPPLE_REQUIRE(effectDeleteQueries.totalQueryCount() == 0);

  project::ProjectController effectKeyframeProject{
    project::createEmptyProject(foundation::ProjectId{"proj_agent_effect_keyframe"}, "Agent Effect Keyframe Project")
  };
  const auto effectKeyframeInitial = effectKeyframeProject.snapshot();
  GRAPPLE_REQUIRE(effectKeyframeInitial);
  const auto effectKeyframeComposition = effectKeyframeProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_keyframe_composition"},
    foundation::ProjectId{"proj_agent_effect_keyframe"},
    effectKeyframeInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect_keyframe"}, "agent"},
    project::CreateCompositionCommand{foundation::NodeId{"node_effect_keyframe_composition"}, "Effect Keyframe Main"}
  });
  GRAPPLE_REQUIRE(effectKeyframeComposition);
  const auto effectKeyframeCamera = effectKeyframeProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_keyframe_camera"},
    foundation::ProjectId{"proj_agent_effect_keyframe"},
    effectKeyframeComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect_keyframe"}, "agent"},
    project::CreateCameraCommand{
      foundation::NodeId{"node_effect_keyframe_camera"},
      foundation::NodeId{"node_effect_keyframe_composition"},
      foundation::EdgeId{"edge_effect_keyframe_contains_camera"},
      timeline::CameraPayload{"Effect Keyframe Camera", timeline::Transform2D{}, timeline::CameraLens{35.0}}
    }
  });
  GRAPPLE_REQUIRE(effectKeyframeCamera);
  const std::string effectKeyframeSource = "def prepare(ctx):\n  return {}\n";
  const auto effectKeyframeEffect = effectKeyframeProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_keyframe_effect"},
    foundation::ProjectId{"proj_agent_effect_keyframe"},
    effectKeyframeCamera.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect_keyframe"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_effect_keyframe_effect"},
      foundation::NodeId{"node_effect_keyframe_camera"},
      foundation::EdgeId{"edge_effect_keyframe_targets"},
      timeline::EffectPayload{
        "Keyframe Me",
        timeline::EffectImplementation{
          timeline::EffectImplementationKind::Python,
          "prepare",
          timeline::EffectSource{
            timeline::EffectSourceKind::InlineSource,
            "python",
            effectKeyframeSource,
            std::nullopt,
            foundation::stableHash(effectKeyframeSource)
          }
        },
        timeline::EffectPortSet{
          {timeline::EffectPort{"frame"}},
          {timeline::EffectPort{"camera_transform"}}
        },
        timeline::ParamSet{
          {
            timeline::Param{
              "target_x",
              0.25,
              timeline::Param::Control{
                "Target X",
                timeline::Param::NumericControl{0.0, 1.0, 0.01}
              },
              {}
            }
          }
        },
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
        {}
      },
      graph::PortName{"camera_transform"},
      graph::PortName{"input"},
      0
    }
  });
  GRAPPLE_REQUIRE(effectKeyframeEffect);

  TestAgentCommandService effectKeyframeCommands{effectKeyframeProject};
  TestAgentQueryService effectKeyframeQueries{effectKeyframeProject};
  TestProjectIdAllocator effectKeyframeIds;
  agent::AgentToolContext effectKeyframeContext{effectKeyframeCommands, effectKeyframeQueries, effectKeyframeIds};

  const agent::AgentTool* createEffectKeyframe = registry.findBySerializedId("effect.create_param_keyframe");
  GRAPPLE_REQUIRE(createEffectKeyframe != nullptr);
  const auto createEffectKeyframeResult = createEffectKeyframe->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_create_param_keyframe"},
      foundation::RunId{"run_effect_keyframe"},
      foundation::ProjectId{"proj_agent_effect_keyframe"},
      effectKeyframeEffect.value().afterRevision,
      R"({
        "effectNodeId": "node_effect_keyframe_effect",
        "paramName": "target_x",
        "time": 1.25,
        "value": 0.5
      })"
    },
    effectKeyframeContext
  );
  GRAPPLE_REQUIRE(createEffectKeyframeResult);
  GRAPPLE_REQUIRE(createEffectKeyframeResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(createEffectKeyframeResult.value().observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(createEffectKeyframeResult.value().payload == "{\"commandId\":\"cmd_agent_1\",\"effectNodeId\":\"node_effect_keyframe_effect\",\"paramName\":\"target_x\",\"keyframeId\":\"key_agent_target_x_1\",\"revision\":\"rev_4\"}");

  const auto afterCreateEffectKeyframeSnapshot = effectKeyframeProject.snapshot();
  GRAPPLE_REQUIRE(afterCreateEffectKeyframeSnapshot);
  const graph::GraphNode* keyframedEffectNode = afterCreateEffectKeyframeSnapshot.value().graph.findNode(foundation::NodeId{"node_effect_keyframe_effect"});
  GRAPPLE_REQUIRE(keyframedEffectNode != nullptr);
  const auto* keyframedEffectPayload = std::get_if<timeline::EffectPayload>(&keyframedEffectNode->payload);
  GRAPPLE_REQUIRE(keyframedEffectPayload != nullptr);
  GRAPPLE_REQUIRE(keyframedEffectPayload->params.values[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(keyframedEffectPayload->params.values[0].keyframes[0].id == foundation::KeyframeId{"key_agent_target_x_1"});
  GRAPPLE_REQUIRE(keyframedEffectPayload->params.values[0].keyframes[0].time == foundation::TimeSeconds{1.25});
  GRAPPLE_REQUIRE(std::get<double>(keyframedEffectPayload->params.values[0].keyframes[0].value) == 0.5);

  const agent::AgentTool* updateEffectKeyframe = registry.findBySerializedId("effect.update_param_keyframe");
  GRAPPLE_REQUIRE(updateEffectKeyframe != nullptr);
  const auto updateEffectKeyframeResult = updateEffectKeyframe->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_update_param_keyframe"},
      foundation::RunId{"run_effect_keyframe"},
      foundation::ProjectId{"proj_agent_effect_keyframe"},
      createEffectKeyframeResult.value().observedRevision,
      R"({
        "effectNodeId": "node_effect_keyframe_effect",
        "paramName": "target_x",
        "keyframeId": "key_agent_target_x_1",
        "time": 2.5,
        "value": 0.75
      })"
    },
    effectKeyframeContext
  );
  GRAPPLE_REQUIRE(updateEffectKeyframeResult);
  GRAPPLE_REQUIRE(updateEffectKeyframeResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(updateEffectKeyframeResult.value().observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(updateEffectKeyframeResult.value().payload == "{\"commandId\":\"cmd_agent_2\",\"effectNodeId\":\"node_effect_keyframe_effect\",\"paramName\":\"target_x\",\"keyframeId\":\"key_agent_target_x_1\",\"revision\":\"rev_5\"}");

  const auto afterUpdateEffectKeyframeSnapshot = effectKeyframeProject.snapshot();
  GRAPPLE_REQUIRE(afterUpdateEffectKeyframeSnapshot);
  const graph::GraphNode* updatedKeyframedEffectNode = afterUpdateEffectKeyframeSnapshot.value().graph.findNode(foundation::NodeId{"node_effect_keyframe_effect"});
  GRAPPLE_REQUIRE(updatedKeyframedEffectNode != nullptr);
  const auto* updatedKeyframedEffectPayload = std::get_if<timeline::EffectPayload>(&updatedKeyframedEffectNode->payload);
  GRAPPLE_REQUIRE(updatedKeyframedEffectPayload != nullptr);
  GRAPPLE_REQUIRE(updatedKeyframedEffectPayload->params.values[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(updatedKeyframedEffectPayload->params.values[0].keyframes[0].id == foundation::KeyframeId{"key_agent_target_x_1"});
  GRAPPLE_REQUIRE(updatedKeyframedEffectPayload->params.values[0].keyframes[0].time == foundation::TimeSeconds{2.5});
  GRAPPLE_REQUIRE(std::get<double>(updatedKeyframedEffectPayload->params.values[0].keyframes[0].value) == 0.75);

  const agent::AgentTool* deleteEffectKeyframe = registry.findBySerializedId("effect.delete_param_keyframe");
  GRAPPLE_REQUIRE(deleteEffectKeyframe != nullptr);
  const auto deleteEffectKeyframeResult = deleteEffectKeyframe->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_effect_delete_param_keyframe"},
      foundation::RunId{"run_effect_keyframe"},
      foundation::ProjectId{"proj_agent_effect_keyframe"},
      updateEffectKeyframeResult.value().observedRevision,
      R"({
        "effectNodeId": "node_effect_keyframe_effect",
        "paramName": "target_x",
        "keyframeId": "key_agent_target_x_1"
      })"
    },
    effectKeyframeContext
  );
  GRAPPLE_REQUIRE(deleteEffectKeyframeResult);
  GRAPPLE_REQUIRE(deleteEffectKeyframeResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(deleteEffectKeyframeResult.value().observedRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(deleteEffectKeyframeResult.value().payload == "{\"commandId\":\"cmd_agent_3\",\"effectNodeId\":\"node_effect_keyframe_effect\",\"paramName\":\"target_x\",\"keyframeId\":\"key_agent_target_x_1\",\"revision\":\"rev_6\"}");

  const auto afterDeleteEffectKeyframeSnapshot = effectKeyframeProject.snapshot();
  GRAPPLE_REQUIRE(afterDeleteEffectKeyframeSnapshot);
  const graph::GraphNode* unkeyframedEffectNode = afterDeleteEffectKeyframeSnapshot.value().graph.findNode(foundation::NodeId{"node_effect_keyframe_effect"});
  GRAPPLE_REQUIRE(unkeyframedEffectNode != nullptr);
  const auto* unkeyframedEffectPayload = std::get_if<timeline::EffectPayload>(&unkeyframedEffectNode->payload);
  GRAPPLE_REQUIRE(unkeyframedEffectPayload != nullptr);
  GRAPPLE_REQUIRE(unkeyframedEffectPayload->params.values[0].keyframes.empty());
  GRAPPLE_REQUIRE(effectKeyframeCommands.applyCount() == 3);
  GRAPPLE_REQUIRE(effectKeyframeQueries.totalQueryCount() == 0);

  project::ProjectController clipTransformProject{
    project::createEmptyProject(foundation::ProjectId{"proj_agent_clip_transform"}, "Agent Clip Transform Project")
  };
  const auto clipTransformInitial = clipTransformProject.snapshot();
  GRAPPLE_REQUIRE(clipTransformInitial);
  const auto clipTransformComposition = clipTransformProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip_transform_composition"},
    foundation::ProjectId{"proj_agent_clip_transform"},
    clipTransformInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_clip_transform"}, "agent"},
    project::CreateCompositionCommand{foundation::NodeId{"node_clip_transform_composition"}, "Clip Transform Main"}
  });
  GRAPPLE_REQUIRE(clipTransformComposition);
  const auto clipTransformAsset = clipTransformProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip_transform_asset"},
    foundation::ProjectId{"proj_agent_clip_transform"},
    clipTransformComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_clip_transform"}, "agent"},
    project::RegisterAssetCommand{
      asset::Asset{
        foundation::AssetId{"asset_clip_transform_video"},
        "Clip Transform Video",
        asset::AssetMetadata{
          asset::AssetMediaType::Video,
          foundation::FilePath{"/media/clip-transform.mov"},
          std::nullopt,
          foundation::TimeSeconds{10.0},
          foundation::Resolution{1080, 1920},
          foundation::FrameRate{30, 1}
        }
      }
    }
  });
  GRAPPLE_REQUIRE(clipTransformAsset);
  const auto clipTransformTrack = clipTransformProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip_transform_track"},
    foundation::ProjectId{"proj_agent_clip_transform"},
    clipTransformAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_clip_transform"}, "agent"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_clip_transform_track"},
      foundation::NodeId{"node_clip_transform_composition"},
      foundation::EdgeId{"edge_clip_transform_track"},
      "Clip Transform Track",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(clipTransformTrack);
  const auto clipTransformClip = clipTransformProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip_transform_clip"},
    foundation::ProjectId{"proj_agent_clip_transform"},
    clipTransformTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_clip_transform"}, "agent"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip_transform_clip"},
      foundation::NodeId{"node_clip_transform_track"},
      foundation::EdgeId{"edge_clip_transform_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{2.0}, foundation::TimeSeconds{6.0}},
        foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{5.0}},
        1.25,
        foundation::AssetId{"asset_clip_transform_video"},
        timeline::Transform2D{
          foundation::Vec2{0.0, 0.0},
          foundation::Vec2{1.0, 1.0},
          0.0,
          1.0
        }
      }
    }
  });
  GRAPPLE_REQUIRE(clipTransformClip);

  TestAgentCommandService clipTransformCommands{clipTransformProject};
  TestAgentQueryService clipTransformQueries{clipTransformProject};
  TestProjectIdAllocator clipTransformIds;
  agent::AgentToolContext clipTransformContext{clipTransformCommands, clipTransformQueries, clipTransformIds};

  const agent::AgentTool* updateClipTransform = registry.findBySerializedId("timeline.update_clip_transform");
  GRAPPLE_REQUIRE(updateClipTransform != nullptr);
  const auto updateClipTransformResult = updateClipTransform->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_update_clip_transform"},
      foundation::RunId{"run_clip_transform"},
      foundation::ProjectId{"proj_agent_clip_transform"},
      clipTransformClip.value().afterRevision,
      R"({
        "clipNodeId": "node_clip_transform_clip",
        "position": {"x": 0.25, "y": -0.5},
        "scale": {"x": 1.5, "y": 0.75},
        "rotationDegrees": 15,
        "opacity": 0.6
      })"
    },
    clipTransformContext
  );
  GRAPPLE_REQUIRE(updateClipTransformResult);
  GRAPPLE_REQUIRE(updateClipTransformResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(updateClipTransformResult.value().observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(updateClipTransformResult.value().payload == "{\"commandId\":\"cmd_agent_1\",\"clipNodeId\":\"node_clip_transform_clip\",\"revision\":\"rev_5\"}");

  const auto afterClipTransformSnapshot = clipTransformProject.snapshot();
  GRAPPLE_REQUIRE(afterClipTransformSnapshot);
  const graph::GraphNode* transformedClipNode = afterClipTransformSnapshot.value().graph.findNode(foundation::NodeId{"node_clip_transform_clip"});
  GRAPPLE_REQUIRE(transformedClipNode != nullptr);
  const auto* transformedClipPayload = std::get_if<timeline::ClipPayload>(&transformedClipNode->payload);
  GRAPPLE_REQUIRE(transformedClipPayload != nullptr);
  GRAPPLE_REQUIRE(transformedClipPayload->kind == timeline::ClipKind::Video);
  GRAPPLE_REQUIRE(transformedClipPayload->timelineRange.start == foundation::TimeSeconds{2.0});
  GRAPPLE_REQUIRE(transformedClipPayload->timelineRange.end == foundation::TimeSeconds{6.0});
  GRAPPLE_REQUIRE(transformedClipPayload->sourceRange.start == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(transformedClipPayload->sourceRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(transformedClipPayload->playbackRate == 1.25);
  GRAPPLE_REQUIRE(transformedClipPayload->assetId == foundation::AssetId{"asset_clip_transform_video"});
  GRAPPLE_REQUIRE((transformedClipPayload->transform.position == foundation::Vec2{0.25, -0.5}));
  GRAPPLE_REQUIRE((transformedClipPayload->transform.scale == foundation::Vec2{1.5, 0.75}));
  GRAPPLE_REQUIRE(transformedClipPayload->transform.rotationDegrees == 15.0);
  GRAPPLE_REQUIRE(transformedClipPayload->transform.opacity == 0.6);
  GRAPPLE_REQUIRE(clipTransformCommands.applyCount() == 1);
  GRAPPLE_REQUIRE(clipTransformQueries.snapshotQueryCount() == 1);

  project::ProjectController timelineDeleteProject{
    project::createEmptyProject(foundation::ProjectId{"proj_agent_timeline_delete"}, "Agent Timeline Delete Project")
  };
  const auto timelineDeleteInitial = timelineDeleteProject.snapshot();
  GRAPPLE_REQUIRE(timelineDeleteInitial);
  const auto timelineDeleteComposition = timelineDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_timeline_delete_composition"},
    foundation::ProjectId{"proj_agent_timeline_delete"},
    timelineDeleteInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_timeline_delete"}, "agent"},
    project::CreateCompositionCommand{foundation::NodeId{"node_timeline_delete_composition"}, "Timeline Delete Main"}
  });
  GRAPPLE_REQUIRE(timelineDeleteComposition);
  const auto timelineDeleteAsset = timelineDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_timeline_delete_asset"},
    foundation::ProjectId{"proj_agent_timeline_delete"},
    timelineDeleteComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_timeline_delete"}, "agent"},
    project::RegisterAssetCommand{
      asset::Asset{
        foundation::AssetId{"asset_timeline_delete_video"},
        "Timeline Delete Video",
        asset::AssetMetadata{
          asset::AssetMediaType::Video,
          foundation::FilePath{"/media/timeline-delete.mov"},
          std::nullopt,
          foundation::TimeSeconds{10.0},
          foundation::Resolution{1080, 1920},
          foundation::FrameRate{30, 1}
        }
      }
    }
  });
  GRAPPLE_REQUIRE(timelineDeleteAsset);
  const auto timelineDeleteTrack = timelineDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_timeline_delete_track"},
    foundation::ProjectId{"proj_agent_timeline_delete"},
    timelineDeleteAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_timeline_delete"}, "agent"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_timeline_delete_track"},
      foundation::NodeId{"node_timeline_delete_composition"},
      foundation::EdgeId{"edge_timeline_delete_track"},
      "Timeline Delete Track",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(timelineDeleteTrack);
  const auto timelineDeleteClip = timelineDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_timeline_delete_clip"},
    foundation::ProjectId{"proj_agent_timeline_delete"},
    timelineDeleteTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_timeline_delete"}, "agent"},
    project::CreateClipCommand{
      foundation::NodeId{"node_timeline_delete_clip"},
      foundation::NodeId{"node_timeline_delete_track"},
      foundation::EdgeId{"edge_timeline_delete_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}},
        1.0,
        foundation::AssetId{"asset_timeline_delete_video"},
        timeline::Transform2D{}
      }
    }
  });
  GRAPPLE_REQUIRE(timelineDeleteClip);

  TestAgentCommandService timelineDeleteCommands{timelineDeleteProject};
  TestAgentQueryService timelineDeleteQueries{timelineDeleteProject};
  TestProjectIdAllocator timelineDeleteIds;
  agent::AgentToolContext timelineDeleteContext{timelineDeleteCommands, timelineDeleteQueries, timelineDeleteIds};

  const agent::AgentTool* deleteClip = registry.findBySerializedId("timeline.delete_clip");
  GRAPPLE_REQUIRE(deleteClip != nullptr);
  const auto deleteClipResult = deleteClip->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_delete_clip"},
      foundation::RunId{"run_timeline_delete"},
      foundation::ProjectId{"proj_agent_timeline_delete"},
      timelineDeleteClip.value().afterRevision,
      R"({
        "clipNodeId": "node_timeline_delete_clip"
      })"
    },
    timelineDeleteContext
  );
  GRAPPLE_REQUIRE(deleteClipResult);
  GRAPPLE_REQUIRE(deleteClipResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(deleteClipResult.value().observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(deleteClipResult.value().payload == "{\"commandId\":\"cmd_agent_1\",\"clipNodeId\":\"node_timeline_delete_clip\",\"revision\":\"rev_5\"}");

  const auto afterDeleteClipSnapshot = timelineDeleteProject.snapshot();
  GRAPPLE_REQUIRE(afterDeleteClipSnapshot);
  GRAPPLE_REQUIRE(afterDeleteClipSnapshot.value().graph.findNode(foundation::NodeId{"node_timeline_delete_clip"}) == nullptr);
  GRAPPLE_REQUIRE(afterDeleteClipSnapshot.value().graph.findNode(foundation::NodeId{"node_timeline_delete_track"}) != nullptr);
  for (const graph::GraphEdge& edge : afterDeleteClipSnapshot.value().graph.edges()) {
    GRAPPLE_REQUIRE(edge.id != foundation::EdgeId{"edge_timeline_delete_clip"});
    GRAPPLE_REQUIRE(edge.sourceNodeId != foundation::NodeId{"node_timeline_delete_clip"});
    GRAPPLE_REQUIRE(edge.targetNodeId != foundation::NodeId{"node_timeline_delete_clip"});
  }

  const agent::AgentTool* deleteTrack = registry.findBySerializedId("timeline.delete_track");
  GRAPPLE_REQUIRE(deleteTrack != nullptr);
  const auto deleteTrackResult = deleteTrack->handler(
    agent::ToolCall{
      foundation::ToolId{"tool_timeline_delete_track"},
      foundation::RunId{"run_timeline_delete"},
      foundation::ProjectId{"proj_agent_timeline_delete"},
      deleteClipResult.value().observedRevision,
      R"({
        "trackNodeId": "node_timeline_delete_track"
      })"
    },
    timelineDeleteContext
  );
  GRAPPLE_REQUIRE(deleteTrackResult);
  GRAPPLE_REQUIRE(deleteTrackResult.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(deleteTrackResult.value().observedRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(deleteTrackResult.value().payload == "{\"commandId\":\"cmd_agent_2\",\"trackNodeId\":\"node_timeline_delete_track\",\"revision\":\"rev_6\"}");

  const auto afterDeleteTrackSnapshot = timelineDeleteProject.snapshot();
  GRAPPLE_REQUIRE(afterDeleteTrackSnapshot);
  GRAPPLE_REQUIRE(afterDeleteTrackSnapshot.value().graph.findNode(foundation::NodeId{"node_timeline_delete_track"}) == nullptr);
  for (const graph::GraphEdge& edge : afterDeleteTrackSnapshot.value().graph.edges()) {
    GRAPPLE_REQUIRE(edge.id != foundation::EdgeId{"edge_timeline_delete_track"});
    GRAPPLE_REQUIRE(edge.sourceNodeId != foundation::NodeId{"node_timeline_delete_track"});
    GRAPPLE_REQUIRE(edge.targetNodeId != foundation::NodeId{"node_timeline_delete_track"});
  }
  GRAPPLE_REQUIRE(timelineDeleteCommands.applyCount() == 2);
  GRAPPLE_REQUIRE(timelineDeleteQueries.totalQueryCount() == 0);

  return 0;
}
