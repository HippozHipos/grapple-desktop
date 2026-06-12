#include <grapple/graph/GraphNode.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <TestAssert.hpp>

#include <algorithm>
#include <optional>
#include <string>
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
    foundation::ProjectId{"proj_projection"},
    initialSnapshot.value().document.revision,
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

  const std::string serializedPlan = projection::serializeCanonicalRenderPlan(planResult.value().plan);
  GRAPPLE_REQUIRE(serializedPlan.find("\"projectId\":\"proj_projection\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"inlineSource\":\"def prepare(ctx):\\n  return {'x': 1}\\n\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"name\":\"target_x\",\"value\":0.77000000000000002") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"sourceEdgeId\":\"edge_effect_targets_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedPlan.find("\"inputs\":[{\"name\":\"input_frame\"}]") != std::string::npos);

  projection::RenderPlan orderedPlan = planResult.value().plan;
  orderedPlan.layers.push_back(projection::RenderLayer{
    foundation::NodeId{"node_alpha_track"},
    "Alpha",
    true
  });
  orderedPlan.clips.push_back(projection::RenderClip{
    foundation::NodeId{"node_alpha_clip"},
    foundation::NodeId{"node_alpha_track"},
    clipPayload,
    true
  });
  orderedPlan.cameras.push_back(projection::RenderCamera{
    foundation::NodeId{"node_alpha_camera"},
    "Alpha Camera",
    cameraPayload.transform,
    cameraPayload.lens,
    true
  });
  orderedPlan.effectGraphs.push_back(projection::RenderEffectGraph{
    foundation::GraphId{"effect_graph_node_alpha_camera"},
    foundation::NodeId{"node_alpha_camera"},
    {
      projection::RenderEffectNode{
        foundation::NodeId{"node_alpha_effect"},
        effectPayload,
        true
      }
    },
    {
      projection::RenderEffectEdge{
        foundation::EdgeId{"edge_alpha_effect_targets_camera"},
        foundation::NodeId{"node_alpha_effect"},
        foundation::NodeId{"node_alpha_camera"},
        true
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
    "{\"effectNodeId\":\"node_effect\",\"params\":[{\"name\":\"target_x\",\"value\":0.5},{\"name\":\"subject_height\",\"value\":0.80000000000000004}]}"
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
    project::ProjectSnapshot{untargetedEffectDocument}
  });
  GRAPPLE_REQUIRE(!untargetedEffectTimeline);
  GRAPPLE_REQUIRE(untargetedEffectTimeline.error().code == "projection.effect_target_missing");

  return 0;
}
