#include <grapple/graph/GraphNode.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <TestAssert.hpp>

#include <optional>
#include <variant>

int main() {
  using namespace grapple;

  project::ProjectController controller{
    project::createEmptyProject(foundation::ProjectId{"proj_projection"}, "Projection Test")
  };

  const auto initialSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(initialSnapshot);

  const auto createComposition = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_composition"},
    project::CommandKind::CreateComposition,
    foundation::ProjectId{"proj_projection"},
    initialSnapshot.value().document.revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(createComposition);

  const auto createTrack = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_track"},
    project::CommandKind::CreateTrack,
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
    project::CommandKind::CreateClip,
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
    project::CommandKind::CreateCamera,
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
    project::CommandKind::CreateEffect,
    foundation::ProjectId{"proj_projection"},
    createCamera.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_1"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_effect"},
      foundation::NodeId{"node_camera"},
      foundation::EdgeId{"edge_effect_targets_camera"},
      effectPayload
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
  GRAPPLE_REQUIRE(planResult.value().plan.effectGraphs[0].edges[0].targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(planResult.value().diagnostics.empty());

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
    project::ProjectSnapshot{malformedDocument}
  });
  GRAPPLE_REQUIRE(!malformedTimeline);
  GRAPPLE_REQUIRE(malformedTimeline.error().code == "projection.clip_track_missing");

  return 0;
}
