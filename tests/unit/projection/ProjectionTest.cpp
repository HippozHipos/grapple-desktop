#include <grapple/asset/AssetSerializer.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/projection/ProjectionQueryService.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/RenderPlanHashes.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <TestAssert.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

class CountingProjectQueryService final : public grapple::project::IProjectQueryService {
public:
  explicit CountingProjectQueryService(grapple::project::ProjectSnapshot snapshot)
    : snapshot_(std::move(snapshot)) {}

  grapple::foundation::Result<grapple::project::ProjectQueryResult> query(
    const grapple::project::ProjectQuery& query
  ) const override {
    ++queryCount;
    if (std::holds_alternative<grapple::project::GetProjectSnapshotQuery>(query)) {
      return grapple::project::ProjectQueryResult{
        grapple::project::ProjectSnapshotResult{snapshot_}
      };
    }

    return grapple::foundation::Error{
      "test.unexpected_query",
      "Projection query service should request the project snapshot."
    };
  }

  mutable int queryCount = 0;

private:
  grapple::project::ProjectSnapshot snapshot_;
};

void requireNoSerializedRuntimeOutputs(const std::string& serializedPlan) {
  const std::vector<std::string> forbiddenRuntimeFields{
    "\"runtimeDiagnostics\"",
    "\"renderDiagnostics\"",
    "\"preparedPlanHash\"",
    "\"compiledModule\"",
    "\"compiledShader\"",
    "\"decodedFrame\"",
    "\"modelResponse\"",
    "\"cacheHandle\"",
    "\"mask\"",
    "\"depthTexture\"",
    "\"motionVector\""
  };

  for (const std::string& field : forbiddenRuntimeFields) {
    GRAPPLE_REQUIRE(serializedPlan.find(field) == std::string::npos);
  }
}

} // namespace

int main() {
  using namespace grapple;

  project::ProjectController controller{
    project::createEmptyProject(foundation::ProjectId{"proj_projection"}, "Projection Test")
  };

  const auto initialSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(initialSnapshot);

  const auto createComposition = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_composition"},
    foundation::ProjectId{"proj_projection"},
    initialSnapshot.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(createComposition);

  const auto createTrack = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_track"},
    foundation::ProjectId{"proj_projection"},
    createComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_track"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_track"},
      "Video",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(createTrack);

  const auto registerClipAsset = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_register_clip_asset"},
    foundation::ProjectId{"proj_projection"},
    createTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "test"},
    project::RegisterAssetCommand{asset::Asset{
      foundation::AssetId{"asset_walking_woman"},
      "Walking Woman",
      asset::AssetMetadata{
        asset::AssetMediaType::Video,
        foundation::FilePath{"/media/walking-woman.mp4"},
        std::nullopt,
        foundation::TimeSeconds{10.0},
        foundation::Resolution{1920, 1080},
        foundation::FrameRate{30, 1}
      }
    }}
  });
  GRAPPLE_REQUIRE(registerClipAsset);

  const timeline::ClipPayload clipPayload{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
    foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{11.0}},
    1.0,
    foundation::AssetId{"asset_walking_woman"},
    timeline::Transform2D{}
  };
  const auto createClip = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip"},
    foundation::ProjectId{"proj_projection"},
    registerClipAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip"},
      foundation::NodeId{"node_track"},
      foundation::EdgeId{"edge_contains_clip"},
      clipPayload
    }
  });
  GRAPPLE_REQUIRE(createClip);

  const auto createTextClip = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_text_clip"},
    foundation::ProjectId{"proj_projection"},
    createClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTextClipCommand{
      foundation::NodeId{"node_text_clip"},
      foundation::NodeId{"node_track"},
      foundation::EdgeId{"edge_contains_text_clip"},
      timeline::TextClipPayload{
        "Title Card",
        foundation::TimeRange{foundation::TimeSeconds{2.0}, foundation::TimeSeconds{6.0}},
        timeline::Transform2D{
          foundation::Vec2{0.0, 0.4},
          foundation::Vec2{1.0, 1.0},
          0.0,
          1.0
        },
        timeline::TextClipStyle{44.0, foundation::Vec3{1.0, 1.0, 1.0}}
      }
    }
  });
  GRAPPLE_REQUIRE(createTextClip);

  const timeline::CameraPayload cameraPayload{
    "Camera",
    timeline::CameraState{
      timeline::Transform2D{},
      timeline::CameraLens{35.0}
    }
  };
  const auto createCamera = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_camera"},
    foundation::ProjectId{"proj_projection"},
    createTextClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCameraCommand{
      foundation::NodeId{"node_camera"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_camera"},
      cameraPayload
    }
  });
  GRAPPLE_REQUIRE(createCamera);

  const timeline::EffectPayload effectPayload{
    "Subject Follow",
    timeline::EffectImplementation{
      timeline::EffectImplementationKind::Python,
      "prepare",
      timeline::EffectSource{
        timeline::EffectSourceKind::InlineSource,
        "python",
        "def prepare(ctx):\n  return {'x': 1}\n",
        std::nullopt,
        foundation::stableHash("def prepare(ctx):\n  return {'x': 1}\n")
      }
    },
    timeline::EffectPortSet{
      {timeline::EffectPort{"input_frame"}},
      {timeline::EffectPort{"camera_transform"}}
    },
    timeline::ParamSet{
      {timeline::Param{
        "target_x",
        0.77,
        timeline::Param::Control{
          "Target X",
          timeline::Param::NumericControl{0.0, 1.0, 0.01}
        },
        {
          timeline::Param::Keyframe{
            foundation::KeyframeId{"key_target_x_1"},
            foundation::TimeSeconds{1.0},
            0.8
          }
        }
      }}
    },
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}}
  };
  const auto createEffect = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect"},
    foundation::ProjectId{"proj_projection"},
    createCamera.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_1"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_effect"},
      foundation::NodeId{"node_camera"},
      foundation::EdgeId{"edge_effect_targets_camera"},
      effectPayload,
      graph::PortName{"camera_transform"},
      graph::PortName{"input"},
      7
    }
  });
  GRAPPLE_REQUIRE(createEffect);

  const std::string shaderSource = "void mainImage(out vec4 color, in vec2 uv) {\n  color = vec4(uv, 0.0, 1.0);\n}\n";
  const timeline::EffectPayload shaderEffectPayload{
    "Vignette Shader",
    timeline::EffectImplementation{
      timeline::EffectImplementationKind::Shader,
      "mainImage",
      timeline::EffectSource{
        timeline::EffectSourceKind::InlineSource,
        "glsl",
        shaderSource,
        std::nullopt,
        foundation::stableHash(shaderSource)
      }
    },
    timeline::EffectPortSet{
      {timeline::EffectPort{"input_frame"}},
      {timeline::EffectPort{"color"}}
    },
    timeline::ParamSet{
      {timeline::Param{
        "strength",
        0.4,
        timeline::Param::Control{
          "Strength",
          timeline::Param::NumericControl{0.0, 1.0, 0.05}
        }
      }}
    },
    foundation::TimeRange{foundation::TimeSeconds{2.0}, foundation::TimeSeconds{8.0}}
  };
  const auto createShaderEffect = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_shader_effect"},
    foundation::ProjectId{"proj_projection"},
    createEffect.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_1"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_shader_effect"},
      foundation::NodeId{"node_camera"},
      foundation::EdgeId{"edge_shader_effect_targets_camera"},
      shaderEffectPayload,
      graph::PortName{"color"},
      graph::PortName{"input"},
      9
    }
  });
  GRAPPLE_REQUIRE(createShaderEffect);

  const auto snapshot = controller.snapshot();
  GRAPPLE_REQUIRE(snapshot);

  const projection::TimelineProjector projector;
  const auto timelineResult = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    snapshot.value()
  });
  GRAPPLE_REQUIRE(timelineResult);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.projectId == foundation::ProjectId{"proj_projection"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.revision == foundation::RevisionId{"rev_8"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.duration == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.layers.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.audioTracks.empty());
  GRAPPLE_REQUIRE(timelineResult.value().timeline.layers[0].sourceNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.textClips.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.audioClips.empty());
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips[0].sourceNodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips[0].trackNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips[0].payload.assetId == foundation::AssetId{"asset_walking_woman"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.textClips[0].sourceNodeId == foundation::NodeId{"node_text_clip"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.textClips[0].trackNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.textClips[0].payload.text == "Title Card");
  GRAPPLE_REQUIRE(timelineResult.value().timeline.cameras.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.cameras[0].sourceNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.cameras[0].state.lens.focalLength == 35.0);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.effectGraphs[0].targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.effectGraphs[0].nodes.size() == 2);
  const auto timelinePythonEffect = std::find_if(
    timelineResult.value().timeline.effectGraphs[0].nodes.begin(),
    timelineResult.value().timeline.effectGraphs[0].nodes.end(),
    [](const projection::TimelineEffectNode& node) {
      return node.sourceNodeId == foundation::NodeId{"node_effect"};
    }
  );
  GRAPPLE_REQUIRE(timelinePythonEffect != timelineResult.value().timeline.effectGraphs[0].nodes.end());
  GRAPPLE_REQUIRE(timelinePythonEffect->payload.implementation.source.inlineSource == "def prepare(ctx):\n  return {'x': 1}\n");
  const auto timelineShaderEffect = std::find_if(
    timelineResult.value().timeline.effectGraphs[0].nodes.begin(),
    timelineResult.value().timeline.effectGraphs[0].nodes.end(),
    [](const projection::TimelineEffectNode& node) {
      return node.sourceNodeId == foundation::NodeId{"node_shader_effect"};
    }
  );
  GRAPPLE_REQUIRE(timelineShaderEffect != timelineResult.value().timeline.effectGraphs[0].nodes.end());
  GRAPPLE_REQUIRE(timelineShaderEffect->payload.implementation.kind == timeline::EffectImplementationKind::Shader);
  GRAPPLE_REQUIRE(timelineShaderEffect->payload.implementation.entrypoint == "mainImage");
  GRAPPLE_REQUIRE(timelineShaderEffect->payload.implementation.source.language == "glsl");
  GRAPPLE_REQUIRE(timelineShaderEffect->payload.implementation.source.inlineSource == shaderSource);
  GRAPPLE_REQUIRE(timelineResult.value().diagnostics.empty());

  const projection::RenderPlanBuilder builder;
  const auto planResult = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    timelineResult.value().timeline
  });
  GRAPPLE_REQUIRE(planResult);
  GRAPPLE_REQUIRE(planResult.value().plan.projectId == foundation::ProjectId{"proj_projection"});
  GRAPPLE_REQUIRE(planResult.value().plan.revision == foundation::RevisionId{"rev_8"});
  GRAPPLE_REQUIRE(planResult.value().plan.duration == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(planResult.value().plan.layers.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.audioTracks.empty());
  GRAPPLE_REQUIRE(planResult.value().plan.layers[0].sourceNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(planResult.value().plan.clips.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.textClips.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.audioClips.empty());
  GRAPPLE_REQUIRE(planResult.value().plan.clips[0].payload.timelineRange.end == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(planResult.value().plan.textClips[0].payload.style.fontSize == 44.0);
  GRAPPLE_REQUIRE(planResult.value().plan.cameras.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].id == foundation::GraphId{"effect_graph_node_camera"});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes.size() == 2);
  const auto planPythonEffect = std::find_if(
    planResult.value().plan.effectGraphs[0].nodes.begin(),
    planResult.value().plan.effectGraphs[0].nodes.end(),
    [](const projection::RenderEffectNode& node) {
      return node.sourceNodeId == foundation::NodeId{"node_effect"};
    }
  );
  GRAPPLE_REQUIRE(planPythonEffect != planResult.value().plan.effectGraphs[0].nodes.end());
  GRAPPLE_REQUIRE(planPythonEffect->payload.implementation.kind == timeline::EffectImplementationKind::Python);
  GRAPPLE_REQUIRE(planPythonEffect->payload.implementation.entrypoint == "prepare");
  GRAPPLE_REQUIRE(planPythonEffect->payload.ports.inputs[0].name == "input_frame");
  GRAPPLE_REQUIRE(planPythonEffect->payload.ports.outputs[0].name == "camera_transform");
  GRAPPLE_REQUIRE(std::get<double>(planPythonEffect->payload.params.values[0].value) == 0.77);
  GRAPPLE_REQUIRE(planPythonEffect->payload.params.values[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(planPythonEffect->payload.params.values[0].keyframes[0].id == foundation::KeyframeId{"key_target_x_1"});
  GRAPPLE_REQUIRE(planPythonEffect->payload.params.values[0].keyframes[0].time == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(std::get<double>(planPythonEffect->payload.params.values[0].keyframes[0].value) == 0.8);
  GRAPPLE_REQUIRE(planPythonEffect->payload.activeRange.end == foundation::TimeSeconds{10.0});
  const auto planShaderEffect = std::find_if(
    planResult.value().plan.effectGraphs[0].nodes.begin(),
    planResult.value().plan.effectGraphs[0].nodes.end(),
    [](const projection::RenderEffectNode& node) {
      return node.sourceNodeId == foundation::NodeId{"node_shader_effect"};
    }
  );
  GRAPPLE_REQUIRE(planShaderEffect != planResult.value().plan.effectGraphs[0].nodes.end());
  GRAPPLE_REQUIRE(planShaderEffect->payload.implementation.kind == timeline::EffectImplementationKind::Shader);
  GRAPPLE_REQUIRE(planShaderEffect->payload.implementation.entrypoint == "mainImage");
  GRAPPLE_REQUIRE(planShaderEffect->payload.implementation.source.language == "glsl");
  GRAPPLE_REQUIRE(planShaderEffect->payload.implementation.source.inlineSource == shaderSource);
  GRAPPLE_REQUIRE(planShaderEffect->payload.ports.inputs[0].name == "input_frame");
  GRAPPLE_REQUIRE(planShaderEffect->payload.ports.outputs[0].name == "color");
  GRAPPLE_REQUIRE(planShaderEffect->payload.params.values[0].name == "strength");
  GRAPPLE_REQUIRE(planShaderEffect->payload.params.values[0].control.label == "Strength");
  GRAPPLE_REQUIRE(std::get<double>(planShaderEffect->payload.params.values[0].value) == 0.4);
  GRAPPLE_REQUIRE(planShaderEffect->payload.activeRange.start == foundation::TimeSeconds{2.0});
  GRAPPLE_REQUIRE(planShaderEffect->payload.activeRange.end == foundation::TimeSeconds{8.0});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges.size() == 2);
  const auto planPythonEdge = std::find_if(
    planResult.value().plan.effectGraphs[0].edges.begin(),
    planResult.value().plan.effectGraphs[0].edges.end(),
    [](const projection::RenderEffectEdge& edge) {
      return edge.sourceNodeId == foundation::NodeId{"node_effect"};
    }
  );
  GRAPPLE_REQUIRE(planPythonEdge != planResult.value().plan.effectGraphs[0].edges.end());
  GRAPPLE_REQUIRE(planPythonEdge->sourcePort == graph::PortName{"camera_transform"});
  GRAPPLE_REQUIRE(planPythonEdge->targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(planPythonEdge->targetPort == graph::PortName{"input"});
  GRAPPLE_REQUIRE(planPythonEdge->order == 7);
  const auto planShaderEdge = std::find_if(
    planResult.value().plan.effectGraphs[0].edges.begin(),
    planResult.value().plan.effectGraphs[0].edges.end(),
    [](const projection::RenderEffectEdge& edge) {
      return edge.sourceNodeId == foundation::NodeId{"node_shader_effect"};
    }
  );
  GRAPPLE_REQUIRE(planShaderEdge != planResult.value().plan.effectGraphs[0].edges.end());
  GRAPPLE_REQUIRE(planShaderEdge->sourcePort == graph::PortName{"color"});
  GRAPPLE_REQUIRE(planShaderEdge->targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(planShaderEdge->targetPort == graph::PortName{"input"});
  GRAPPLE_REQUIRE(planShaderEdge->order == 9);
  GRAPPLE_REQUIRE(planResult.value().diagnostics.empty());

  const auto repeatedTimelineResult = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    snapshot.value()
  });
  GRAPPLE_REQUIRE(repeatedTimelineResult);
  const auto repeatedPlanResult = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    repeatedTimelineResult.value().timeline
  });
  GRAPPLE_REQUIRE(repeatedPlanResult);
  GRAPPLE_REQUIRE(
    projection::serializeCanonicalRenderPlan(repeatedPlanResult.value().plan) ==
    projection::serializeCanonicalRenderPlan(planResult.value().plan)
  );

  CountingProjectQueryService projectQueries{snapshot.value()};
  const projection::ProjectionQueryService projectionQueries{projectQueries};
  const auto queriedPlan = projectionQueries.buildCurrentRenderPlan();
  GRAPPLE_REQUIRE(queriedPlan);
  GRAPPLE_REQUIRE(projectQueries.queryCount == 1);
  GRAPPLE_REQUIRE(
    projection::serializeCanonicalRenderPlan(queriedPlan.value().plan) ==
    projection::serializeCanonicalRenderPlan(planResult.value().plan)
  );

  const std::string serializedPlan = projection::serializeCanonicalRenderPlan(planResult.value().plan);
  const std::string serializedPlanContent = projection::serializeCanonicalRenderPlanContent(planResult.value().plan);
  GRAPPLE_REQUIRE(projection::hashRenderPlan(planResult.value().plan) == foundation::stableHash(serializedPlanContent));
  projection::RenderPlan revisionOnlyPlan = planResult.value().plan;
  revisionOnlyPlan.revision = foundation::RevisionId{"rev_revision_only"};
  GRAPPLE_REQUIRE(projection::serializeCanonicalRenderPlan(revisionOnlyPlan) != serializedPlan);
  GRAPPLE_REQUIRE(projection::serializeCanonicalRenderPlanContent(revisionOnlyPlan) == serializedPlanContent);
  GRAPPLE_REQUIRE(projection::hashRenderPlan(revisionOnlyPlan) == projection::hashRenderPlan(planResult.value().plan));
  projection::RenderPlan displayOnlyPlan = planResult.value().plan;
  displayOnlyPlan.stage.name = "Renamed Stage";
  displayOnlyPlan.layers[0].name = "Renamed Layer";
  displayOnlyPlan.cameras[0].name = "Renamed Camera";
  displayOnlyPlan.effectGraphs[0].nodes[0].payload.displayName = "Renamed Effect";
  displayOnlyPlan.effectGraphs[0].nodes[0].payload.params.values[0].control.label = "Renamed Target X";
  displayOnlyPlan.effectGraphs[0].nodes[0].payload.params.values[0].control.numeric =
    timeline::Param::NumericControl{-10.0, 10.0, 0.5};
  GRAPPLE_REQUIRE(projection::serializeCanonicalRenderPlan(displayOnlyPlan) != serializedPlan);
  GRAPPLE_REQUIRE(projection::serializeCanonicalRenderPlanContent(displayOnlyPlan) == serializedPlanContent);
  GRAPPLE_REQUIRE(projection::hashRenderPlan(displayOnlyPlan) == projection::hashRenderPlan(planResult.value().plan));
  GRAPPLE_REQUIRE(serializedPlan.find("\"projectId\":\"proj_projection\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"duration\":10") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"textClips\":[{\"sourceNodeId\":\"node_text_clip\",\"trackNodeId\":\"node_track\",\"payload\":{\"text\":\"Title Card\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"inlineSource\":\"def prepare(ctx):\\n  return {'x': 1}\\n\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"kind\":\"shader\",\"entrypoint\":\"mainImage\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"language\":\"glsl\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"name\":\"strength\",\"label\":\"Strength\",\"numeric\":{\"min\":0,\"max\":1,\"step\":0.050000000000000003},\"value\":0.40000000000000002") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"name\":\"target_x\",\"label\":\"Target X\",\"numeric\":{\"min\":0,\"max\":1,\"step\":0.01},\"value\":0.77000000000000002") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"sourceEdgeId\":\"edge_effect_targets_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"sourceEdgeId\":\"edge_shader_effect_targets_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"sourcePort\":\"camera_transform\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"sourcePort\":\"color\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"targetPort\":\"input\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"inputs\":[{\"name\":\"input_frame\"}]") != std::string::npos);
  requireNoSerializedRuntimeOutputs(serializedPlan);

  project::ProjectController assetProjectionController{
    project::createEmptyProject(foundation::ProjectId{"proj_projection_assets"}, "Projection Asset Test")
  };
  const auto assetProjectionInitial = assetProjectionController.snapshot();
  GRAPPLE_REQUIRE(assetProjectionInitial);
  const asset::Asset projectionAsset{
    foundation::AssetId{"asset_projection_video"},
    "Projection Video",
    asset::AssetMetadata{
      asset::AssetMediaType::Video,
      foundation::FilePath{"/media/projection-video.mp4"},
      std::nullopt,
      foundation::TimeSeconds{5.0},
      foundation::Resolution{1920, 1080},
      foundation::FrameRate{24, 1}
    }
  };
  const auto projectionAssetResult = assetProjectionController.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_projection_asset"},
    foundation::ProjectId{"proj_projection_assets"},
    assetProjectionInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "test"},
    project::RegisterAssetCommand{projectionAsset}
  });
  GRAPPLE_REQUIRE(projectionAssetResult);
  const auto assetProjectionSnapshot = assetProjectionController.snapshot();
  GRAPPLE_REQUIRE(assetProjectionSnapshot);
  const auto assetTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    assetProjectionSnapshot.value()
  });
  GRAPPLE_REQUIRE(assetTimeline);
  GRAPPLE_REQUIRE(assetTimeline.value().timeline.assets.size() == 1);
  GRAPPLE_REQUIRE(assetTimeline.value().timeline.assets[0].assetId == foundation::AssetId{"asset_projection_video"});
  GRAPPLE_REQUIRE(assetTimeline.value().timeline.assets[0].versionHash == foundation::stableHash(asset::serializeCanonicalAsset(projectionAsset)));
  const auto assetPlan = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    assetTimeline.value().timeline
  });
  GRAPPLE_REQUIRE(assetPlan);
  GRAPPLE_REQUIRE(assetPlan.value().plan.assets.size() == 1);
  GRAPPLE_REQUIRE(assetPlan.value().plan.assets[0].versionHash == foundation::stableHash(asset::serializeCanonicalAsset(projectionAsset)));
  GRAPPLE_REQUIRE(projection::serializeCanonicalRenderPlan(assetPlan.value().plan).find("\"assetId\":\"asset_projection_video\"") != std::string::npos);

  project::ProjectController audioProjectionController{
    project::createEmptyProject(foundation::ProjectId{"proj_projection_audio"}, "Projection Audio Test")
  };
  const auto audioInitial = audioProjectionController.snapshot();
  GRAPPLE_REQUIRE(audioInitial);
  const auto audioComposition = audioProjectionController.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_audio_composition"},
    foundation::ProjectId{"proj_projection_audio"},
    audioInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_audio_composition"}, "Audio Main"}
  });
  GRAPPLE_REQUIRE(audioComposition);
  const auto audioTrack = audioProjectionController.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_audio_track"},
    foundation::ProjectId{"proj_projection_audio"},
    audioComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_audio_track"},
      foundation::NodeId{"node_audio_composition"},
      foundation::EdgeId{"edge_audio_contains_track"},
      "Audio",
      timeline::TrackKind::Audio
    }
  });
  GRAPPLE_REQUIRE(audioTrack);
  const auto audioAsset = audioProjectionController.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_audio_asset"},
    foundation::ProjectId{"proj_projection_audio"},
    audioTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "test"},
    project::RegisterAssetCommand{asset::Asset{
      foundation::AssetId{"asset_projection_audio"},
      "Projection Audio",
      asset::AssetMetadata{
        asset::AssetMediaType::Audio,
        foundation::FilePath{"/media/projection-audio.wav"},
        std::nullopt,
        foundation::TimeSeconds{3.0},
        std::nullopt,
        std::nullopt
      }
    }}
  });
  GRAPPLE_REQUIRE(audioAsset);
  const auto audioClip = audioProjectionController.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_audio_clip"},
    foundation::ProjectId{"proj_projection_audio"},
    audioAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_audio_clip"},
      foundation::NodeId{"node_audio_track"},
      foundation::EdgeId{"edge_audio_contains_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Audio,
        foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{4.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}},
        1.0,
        foundation::AssetId{"asset_projection_audio"},
        timeline::Transform2D{}
      }
    }
  });
  GRAPPLE_REQUIRE(audioClip);
  const auto audioSnapshot = audioProjectionController.snapshot();
  GRAPPLE_REQUIRE(audioSnapshot);
  const auto audioTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{audioSnapshot.value()});
  GRAPPLE_REQUIRE(audioTimeline);
  GRAPPLE_REQUIRE(audioTimeline.value().timeline.layers.empty());
  GRAPPLE_REQUIRE(audioTimeline.value().timeline.audioTracks.size() == 1);
  GRAPPLE_REQUIRE(audioTimeline.value().timeline.audioTracks[0].sourceNodeId == foundation::NodeId{"node_audio_track"});
  GRAPPLE_REQUIRE(audioTimeline.value().timeline.clips.empty());
  GRAPPLE_REQUIRE(audioTimeline.value().timeline.audioClips.size() == 1);
  GRAPPLE_REQUIRE(audioTimeline.value().timeline.duration == foundation::TimeSeconds{4.0});
  const auto audioPlan = builder.buildRenderPlan(projection::BuildRenderPlanRequest{audioTimeline.value().timeline});
  GRAPPLE_REQUIRE(audioPlan);
  GRAPPLE_REQUIRE(audioPlan.value().plan.layers.empty());
  GRAPPLE_REQUIRE(audioPlan.value().plan.audioTracks.size() == 1);
  GRAPPLE_REQUIRE(audioPlan.value().plan.audioTracks[0].sourceNodeId == foundation::NodeId{"node_audio_track"});
  GRAPPLE_REQUIRE(audioPlan.value().plan.clips.empty());
  GRAPPLE_REQUIRE(audioPlan.value().plan.audioClips.size() == 1);
  GRAPPLE_REQUIRE(audioPlan.value().plan.audioClips[0].payload.kind == timeline::ClipKind::Audio);
  const std::string audioPlanJson = projection::serializeCanonicalRenderPlan(audioPlan.value().plan);
  GRAPPLE_REQUIRE(audioPlanJson.find("\"audioTracks\":[") != std::string::npos);
  GRAPPLE_REQUIRE(audioPlanJson.find("\"audioClips\":[") != std::string::npos);
  GRAPPLE_REQUIRE(audioPlanJson.find("\"assetId\":\"asset_projection_audio\"") != std::string::npos);

  projection::RenderPlan orderedPlan = planResult.value().plan;
  orderedPlan.layers.push_back(projection::RenderLayer{
    foundation::NodeId{"node_alpha_track"},
    "Alpha"
  });
  orderedPlan.clips.push_back(projection::RenderClip{
    foundation::NodeId{"node_alpha_clip"},
    foundation::NodeId{"node_alpha_track"},
    clipPayload
  });
  orderedPlan.cameras.push_back(projection::RenderCamera{
    foundation::NodeId{"node_alpha_camera"},
    "Alpha Camera",
    cameraPayload.state
  });
  orderedPlan.effectGraphs.push_back(projection::RenderEffectGraph{
    foundation::GraphId{"effect_graph_node_alpha_camera"},
    foundation::NodeId{"node_alpha_camera"},
    {
      projection::RenderEffectNode{
        foundation::NodeId{"node_alpha_effect"},
        effectPayload
      }
    },
    {
      projection::RenderEffectEdge{
        foundation::EdgeId{"edge_alpha_effect_targets_camera"},
        foundation::NodeId{"node_alpha_effect"},
        graph::PortName{"camera_transform"},
        foundation::NodeId{"node_alpha_camera"},
        graph::PortName{"input"},
        0
      }
    }
  });

  projection::RenderPlan reorderedPlan = orderedPlan;
  std::reverse(reorderedPlan.layers.begin(), reorderedPlan.layers.end());
  std::reverse(reorderedPlan.clips.begin(), reorderedPlan.clips.end());
  std::reverse(reorderedPlan.textClips.begin(), reorderedPlan.textClips.end());
  std::reverse(reorderedPlan.cameras.begin(), reorderedPlan.cameras.end());
  std::reverse(reorderedPlan.effectGraphs.begin(), reorderedPlan.effectGraphs.end());

  GRAPPLE_REQUIRE(
    projection::serializeCanonicalRenderPlan(orderedPlan) ==
    projection::serializeCanonicalRenderPlan(reorderedPlan)
  );

  const project::ProjectCommandEnvelope updateEffectParamValue{
    foundation::CommandId{"cmd_update_effect_param_value"},
    foundation::ProjectId{"proj_projection"},
    createShaderEffect.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateEffectParamValueCommand{
      foundation::NodeId{"node_effect"},
      "target_x",
      0.5
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(updateEffectParamValue.payload) == project::CommandKind::UpdateEffectParamValue);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(updateEffectParamValue.payload) ==
    "{\"effectNodeId\":\"node_effect\",\"paramName\":\"target_x\",\"value\":0.5}"
  );

  const auto updateEffectParamValueResult = controller.apply(updateEffectParamValue);
  GRAPPLE_REQUIRE(updateEffectParamValueResult);
  GRAPPLE_REQUIRE(updateEffectParamValueResult.value().afterRevision == foundation::RevisionId{"rev_9"});

  const auto updatedSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(updatedSnapshot);
  const auto updatedTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    updatedSnapshot.value()
  });
  GRAPPLE_REQUIRE(updatedTimeline);
  const auto updatedPlan = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    updatedTimeline.value().timeline
  });
  GRAPPLE_REQUIRE(updatedPlan);
  GRAPPLE_REQUIRE(updatedPlan.value().plan.revision == foundation::RevisionId{"rev_9"});
  GRAPPLE_REQUIRE(updatedPlan.value().plan.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(updatedPlan.value().plan.effectGraphs[0].nodes.size() == 2);
  const auto updatedPythonEffect = std::find_if(
    updatedPlan.value().plan.effectGraphs[0].nodes.begin(),
    updatedPlan.value().plan.effectGraphs[0].nodes.end(),
    [](const projection::RenderEffectNode& node) {
      return node.sourceNodeId == foundation::NodeId{"node_effect"};
    }
  );
  GRAPPLE_REQUIRE(updatedPythonEffect != updatedPlan.value().plan.effectGraphs[0].nodes.end());
  GRAPPLE_REQUIRE(updatedPythonEffect->payload.implementation.entrypoint == "prepare");
  GRAPPLE_REQUIRE(updatedPythonEffect->payload.params.values.size() == 1);
  GRAPPLE_REQUIRE(std::get<double>(updatedPythonEffect->payload.params.values[0].value) == 0.5);
  GRAPPLE_REQUIRE(updatedPythonEffect->payload.params.values[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(updatedPythonEffect->payload.params.values[0].keyframes[0].id == foundation::KeyframeId{"key_target_x_1"});

  project::ProjectDocument connectedEffectsDocument = project::createEmptyProject(
    foundation::ProjectId{"proj_connected_effects"},
    "Connected Effects Projection Test"
  );
  GRAPPLE_REQUIRE(connectedEffectsDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_connected_camera"},
    graph::NodeKind::Camera,
    cameraPayload,
    true
  }));
  GRAPPLE_REQUIRE(connectedEffectsDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_connected_effect_a"},
    graph::NodeKind::Effect,
    effectPayload,
    true
  }));
  GRAPPLE_REQUIRE(connectedEffectsDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_connected_effect_b"},
    graph::NodeKind::Effect,
    effectPayload,
    true
  }));
  GRAPPLE_REQUIRE(connectedEffectsDocument.graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_connected_effect_a_targets_camera"},
    graph::EdgeKind::Targets,
    foundation::NodeId{"node_connected_effect_a"},
    graph::PortName{"camera_transform"},
    foundation::NodeId{"node_connected_camera"},
    graph::PortName{"input"},
    0,
    true
  }));
  GRAPPLE_REQUIRE(connectedEffectsDocument.graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_connected_effect_b_targets_camera"},
    graph::EdgeKind::Targets,
    foundation::NodeId{"node_connected_effect_b"},
    graph::PortName{"camera_transform"},
    foundation::NodeId{"node_connected_camera"},
    graph::PortName{"input"},
    1,
    true
  }));
  GRAPPLE_REQUIRE(connectedEffectsDocument.graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_connected_effects"},
    graph::EdgeKind::Connects,
    foundation::NodeId{"node_connected_effect_a"},
    graph::PortName{"camera_transform"},
    foundation::NodeId{"node_connected_effect_b"},
    graph::PortName{"input_frame"},
    2,
    true
  }));

  const auto connectedTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    project::makeProjectSnapshot(connectedEffectsDocument)
  });
  GRAPPLE_REQUIRE(connectedTimeline);
  const auto connectedPlan = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    connectedTimeline.value().timeline
  });
  GRAPPLE_REQUIRE(connectedPlan);
  GRAPPLE_REQUIRE(connectedPlan.value().plan.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(connectedPlan.value().plan.effectGraphs[0].nodes.size() == 2);
  GRAPPLE_REQUIRE(connectedPlan.value().plan.effectGraphs[0].edges.size() == 3);
  GRAPPLE_REQUIRE(connectedPlan.value().plan.effectGraphs[0].edges[2].sourceEdgeId == foundation::EdgeId{"edge_connected_effects"});
  GRAPPLE_REQUIRE(connectedPlan.value().plan.effectGraphs[0].edges[2].sourcePort == graph::PortName{"camera_transform"});
  GRAPPLE_REQUIRE(connectedPlan.value().plan.effectGraphs[0].edges[2].targetPort == graph::PortName{"input_frame"});

  project::ProjectDocument disabledProjectionDocument = project::createEmptyProject(
    foundation::ProjectId{"proj_disabled_projection"},
    "Disabled Projection Test"
  );
  GRAPPLE_REQUIRE(disabledProjectionDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_disabled_track"},
    graph::NodeKind::Track,
    timeline::TrackPayload{"Video", timeline::TrackKind::Visual},
    true
  }));
  GRAPPLE_REQUIRE(disabledProjectionDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_disabled_clip"},
    graph::NodeKind::Clip,
    clipPayload,
    false
  }));
  GRAPPLE_REQUIRE(disabledProjectionDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_disabled_camera"},
    graph::NodeKind::Camera,
    cameraPayload,
    false
  }));
  GRAPPLE_REQUIRE(disabledProjectionDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_disabled_effect"},
    graph::NodeKind::Effect,
    effectPayload,
    false
  }));
  GRAPPLE_REQUIRE(disabledProjectionDocument.graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_disabled_contains_clip"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_disabled_track"},
    graph::PortName{},
    foundation::NodeId{"node_disabled_clip"},
    graph::PortName{},
    0,
    true
  }));
  GRAPPLE_REQUIRE(disabledProjectionDocument.graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_disabled_effect_targets_track"},
    graph::EdgeKind::Targets,
    foundation::NodeId{"node_disabled_effect"},
    graph::PortName{"output"},
    foundation::NodeId{"node_disabled_track"},
    graph::PortName{"input"},
    0,
    true
  }));
  const auto disabledTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    project::makeProjectSnapshot(disabledProjectionDocument)
  });
  GRAPPLE_REQUIRE(disabledTimeline);
  GRAPPLE_REQUIRE(disabledTimeline.value().timeline.layers.size() == 1);
  GRAPPLE_REQUIRE(disabledTimeline.value().timeline.clips.empty());
  GRAPPLE_REQUIRE(disabledTimeline.value().timeline.cameras.empty());
  GRAPPLE_REQUIRE(disabledTimeline.value().timeline.effectGraphs.empty());
  GRAPPLE_REQUIRE(disabledTimeline.value().timeline.duration == foundation::TimeSeconds{0.0});

  project::ProjectDocument disabledConnectionDocument = connectedEffectsDocument;
  GRAPPLE_REQUIRE(disabledConnectionDocument.graph.removeEdge(foundation::EdgeId{"edge_connected_effects"}));
  GRAPPLE_REQUIRE(disabledConnectionDocument.graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_connected_effects"},
    graph::EdgeKind::Connects,
    foundation::NodeId{"node_connected_effect_a"},
    graph::PortName{"camera_transform"},
    foundation::NodeId{"node_connected_effect_b"},
    graph::PortName{"input_frame"},
    2,
    false
  }));
  const auto disabledConnectionTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    project::makeProjectSnapshot(disabledConnectionDocument)
  });
  GRAPPLE_REQUIRE(disabledConnectionTimeline);
  const auto disabledConnectionPlan = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    disabledConnectionTimeline.value().timeline
  });
  GRAPPLE_REQUIRE(disabledConnectionPlan);
  GRAPPLE_REQUIRE(disabledConnectionPlan.value().plan.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(disabledConnectionPlan.value().plan.effectGraphs[0].nodes.size() == 2);
  GRAPPLE_REQUIRE(disabledConnectionPlan.value().plan.effectGraphs[0].edges.size() == 2);

  project::ProjectDocument malformedDocument = project::createEmptyProject(
    foundation::ProjectId{"proj_malformed"},
    "Malformed Projection Test"
  );
  const auto malformedClip = malformedDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_orphan_clip"},
    graph::NodeKind::Clip,
    clipPayload,
    true
  });
  GRAPPLE_REQUIRE(malformedClip);

  const auto malformedTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    project::makeProjectSnapshot(malformedDocument)
  });
  GRAPPLE_REQUIRE(!malformedTimeline);
  GRAPPLE_REQUIRE(malformedTimeline.error().code == "projection.clip_track_missing");

  project::ProjectDocument untargetedEffectDocument = project::createEmptyProject(
    foundation::ProjectId{"proj_untargeted_effect"},
    "Untargeted Effect Projection Test"
  );
  const auto untargetedEffect = untargetedEffectDocument.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_untargeted_effect"},
    graph::NodeKind::Effect,
    effectPayload,
    true
  });
  GRAPPLE_REQUIRE(untargetedEffect);

  const auto untargetedEffectTimeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    project::makeProjectSnapshot(untargetedEffectDocument)
  });
  GRAPPLE_REQUIRE(!untargetedEffectTimeline);
  GRAPPLE_REQUIRE(untargetedEffectTimeline.error().code == "projection.effect_target_missing");

  return 0;
}
