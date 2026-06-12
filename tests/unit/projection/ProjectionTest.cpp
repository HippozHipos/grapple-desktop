#include <grapple/asset/AssetSerializer.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/projection/ProjectionQueryService.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <TestAssert.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <variant>

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
      "Video"
    }
  });
  GRAPPLE_REQUIRE(createTrack);

  const timeline::ClipPayload clipPayload{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
    foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{11.0}},
    1.0,
    foundation::AssetId{"asset_walking_woman"},
    timeline::Transform{}
  };
  const auto createClip = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip"},
    foundation::ProjectId{"proj_projection"},
    createTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip"},
      foundation::NodeId{"node_track"},
      foundation::EdgeId{"edge_contains_clip"},
      clipPayload
    }
  });
  GRAPPLE_REQUIRE(createClip);

  const timeline::CameraPayload cameraPayload{
    "Camera",
    timeline::Transform{},
    timeline::CameraLens{35.0}
  };
  const auto createCamera = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_camera"},
    foundation::ProjectId{"proj_projection"},
    createClip.value().afterRevision,
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
      {timeline::Param{"target_x", 0.77}}
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

  const auto snapshot = controller.snapshot();
  GRAPPLE_REQUIRE(snapshot);

  const projection::TimelineProjector projector;
  const auto timelineResult = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    snapshot.value()
  });
  GRAPPLE_REQUIRE(timelineResult);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.projectId == foundation::ProjectId{"proj_projection"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.revision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.duration == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.layers.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.layers[0].sourceNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips[0].sourceNodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips[0].trackNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.clips[0].payload.assetId == foundation::AssetId{"asset_walking_woman"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.cameras.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.cameras[0].sourceNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.cameras[0].lens.focalLength == 35.0);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.effectGraphs[0].targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.effectGraphs[0].nodes.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.effectGraphs[0].nodes[0].payload.implementation.source.inlineSource == "def prepare(ctx):\n  return {'x': 1}\n");
  GRAPPLE_REQUIRE(timelineResult.value().diagnostics.empty());

  const projection::RenderPlanBuilder builder;
  const auto planResult = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    timelineResult.value().timeline
  });
  GRAPPLE_REQUIRE(planResult);
  GRAPPLE_REQUIRE(planResult.value().plan.projectId == foundation::ProjectId{"proj_projection"});
  GRAPPLE_REQUIRE(planResult.value().plan.revision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(planResult.value().plan.duration == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(planResult.value().plan.layers.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.layers[0].sourceNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(planResult.value().plan.clips.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.clips[0].payload.timelineRange.end == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(planResult.value().plan.cameras.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].id == foundation::GraphId{"effect_graph_node_camera"});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes[0].sourceNodeId == foundation::NodeId{"node_effect"});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes[0].payload.implementation.kind == timeline::EffectImplementationKind::Python);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes[0].payload.implementation.entrypoint == "prepare");
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes[0].payload.ports.inputs[0].name == "input_frame");
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes[0].payload.ports.outputs[0].name == "camera_transform");
  GRAPPLE_REQUIRE(std::get<double>(planResult.value().plan.effectGraphs[0].nodes[0].payload.params.values[0].value) == 0.77);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].nodes[0].payload.activeRange.end == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges[0].sourceNodeId == foundation::NodeId{"node_effect"});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges[0].sourcePort == graph::PortName{"camera_transform"});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges[0].targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges[0].targetPort == graph::PortName{"input"});
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges[0].order == 7);
  GRAPPLE_REQUIRE(planResult.value().diagnostics.empty());

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
  GRAPPLE_REQUIRE(serializedPlan.find("\"projectId\":\"proj_projection\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"duration\":10") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"inlineSource\":\"def prepare(ctx):\\n  return {'x': 1}\\n\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"name\":\"target_x\",\"value\":0.77000000000000002") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"sourceEdgeId\":\"edge_effect_targets_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"sourcePort\":\"camera_transform\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"targetPort\":\"input\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"inputs\":[{\"name\":\"input_frame\"}]") != std::string::npos);

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
    cameraPayload.transform,
    cameraPayload.lens
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
  std::reverse(reorderedPlan.cameras.begin(), reorderedPlan.cameras.end());
  std::reverse(reorderedPlan.effectGraphs.begin(), reorderedPlan.effectGraphs.end());

  GRAPPLE_REQUIRE(
    projection::serializeCanonicalRenderPlan(orderedPlan) ==
    projection::serializeCanonicalRenderPlan(reorderedPlan)
  );

  const project::ProjectCommandEnvelope setEffectParams{
    foundation::CommandId{"cmd_set_effect_params"},
    foundation::ProjectId{"proj_projection"},
    createEffect.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::SetEffectParamsCommand{
      foundation::NodeId{"node_effect"},
      timeline::ParamSet{
        {timeline::Param{"target_x", 0.5}, timeline::Param{"subject_height", 0.8}}
      }
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(setEffectParams.payload) == project::CommandKind::SetEffectParams);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(setEffectParams.payload) ==
    "{\"effectNodeId\":\"node_effect\",\"params\":[{\"name\":\"subject_height\",\"value\":0.80000000000000004},{\"name\":\"target_x\",\"value\":0.5}]}"
  );

  const auto setEffectParamsResult = controller.apply(setEffectParams);
  GRAPPLE_REQUIRE(setEffectParamsResult);
  GRAPPLE_REQUIRE(setEffectParamsResult.value().afterRevision == foundation::RevisionId{"rev_6"});

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
  GRAPPLE_REQUIRE(updatedPlan.value().plan.revision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(updatedPlan.value().plan.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(updatedPlan.value().plan.effectGraphs[0].nodes.size() == 1);
  GRAPPLE_REQUIRE(updatedPlan.value().plan.effectGraphs[0].nodes[0].payload.implementation.entrypoint == "prepare");
  GRAPPLE_REQUIRE(updatedPlan.value().plan.effectGraphs[0].nodes[0].payload.params.values.size() == 2);
  GRAPPLE_REQUIRE(std::get<double>(updatedPlan.value().plan.effectGraphs[0].nodes[0].payload.params.values[0].value) == 0.5);
  GRAPPLE_REQUIRE(std::get<double>(updatedPlan.value().plan.effectGraphs[0].nodes[0].payload.params.values[1].value) == 0.8);

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
    timeline::TrackPayload{"Video"},
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
