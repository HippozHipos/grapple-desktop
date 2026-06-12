#include <grapple/runtime/RuntimeDependencyPlanner.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/runtime/MemoryRuntimeCache.hpp>

#include <TestAssert.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

grapple::projection::RenderPlan makePlan(std::string layerName) {
  return grapple::projection::RenderPlan{
    grapple::foundation::ProjectId{"proj_runtime"},
    grapple::foundation::RevisionId{"rev_4"},
    grapple::projection::RenderStage{"Runtime Test"},
    grapple::foundation::TimeSeconds{10.0},
    {},
    {
      grapple::projection::RenderLayer{
        grapple::foundation::NodeId{"node_track"},
        std::move(layerName),
        true
      }
    },
    {},
    {},
    {}
  };
}

grapple::projection::RenderPlan makeClipPlan(double playbackRate) {
  grapple::projection::RenderPlan plan = makePlan("Video");
  plan.assets.push_back(grapple::projection::RenderAsset{
    grapple::foundation::AssetId{"asset_video"},
    grapple::foundation::stableHash("asset_video_v1")
  });
  plan.clips.push_back(grapple::projection::RenderClip{
    grapple::foundation::NodeId{"node_clip"},
    grapple::foundation::NodeId{"node_track"},
    grapple::timeline::ClipPayload{
      grapple::timeline::ClipKind::Video,
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, grapple::foundation::TimeSeconds{10.0}},
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{1.0}, grapple::foundation::TimeSeconds{11.0}},
      playbackRate,
      grapple::foundation::AssetId{"asset_video"},
      grapple::timeline::Transform{}
    },
    true
  });
  return plan;
}

grapple::projection::RenderPlan makeClipPlanWithAssetVersion(std::string assetVersion) {
  grapple::projection::RenderPlan plan = makeClipPlan(1.0);
  plan.assets[0].versionHash = grapple::foundation::stableHash(assetVersion);
  return plan;
}

grapple::projection::RenderPlan makeClipPlanWithLayerName(std::string layerName) {
  grapple::projection::RenderPlan plan = makeClipPlan(1.0);
  plan.layers[0].name = std::move(layerName);
  return plan;
}

grapple::projection::RenderPlan makeDisabledClipPlan() {
  grapple::projection::RenderPlan plan = makeClipPlan(1.0);
  plan.clips[0].enabled = false;
  return plan;
}

grapple::projection::RenderPlan makeEffectPlan(std::string source) {
  grapple::projection::RenderPlan plan = makePlan("Video");
  plan.cameras.push_back(grapple::projection::RenderCamera{
    grapple::foundation::NodeId{"node_camera"},
    "Camera",
    grapple::timeline::Transform{},
    grapple::timeline::CameraLens{},
    true
  });
  plan.effectGraphs.push_back(grapple::projection::RenderEffectGraph{
    grapple::foundation::GraphId{"effect_graph_node_camera"},
    grapple::foundation::NodeId{"node_camera"},
    {
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect"},
        grapple::timeline::EffectPayload{
          "Effect",
          grapple::timeline::EffectImplementation{
            grapple::timeline::EffectImplementationKind::Python,
            "prepare",
            grapple::timeline::EffectSource{
              grapple::timeline::EffectSourceKind::InlineSource,
              "python",
              source,
              std::nullopt,
              grapple::foundation::stableHash(source)
            }
          },
          grapple::timeline::EffectPortSet{},
          grapple::timeline::ParamSet{},
          grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, grapple::foundation::TimeSeconds{10.0}}
        },
        true
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_targets_camera"},
        grapple::foundation::NodeId{"node_effect"},
        grapple::graph::PortName{"camera_transform"},
        grapple::foundation::NodeId{"node_camera"},
        grapple::graph::PortName{"input"},
        0,
        true
      }
    }
  });
  return plan;
}

grapple::projection::RenderPlan makeEffectPlanWithCameraX(double cameraX) {
  grapple::projection::RenderPlan plan = makeEffectPlan("def prepare(): pass");
  plan.cameras[0].transform.position.x = cameraX;
  return plan;
}

grapple::projection::RenderPlan makeClipEffectPlan() {
  grapple::projection::RenderPlan plan = makeClipPlan(1.0);
  plan.effectGraphs.push_back(grapple::projection::RenderEffectGraph{
    grapple::foundation::GraphId{"effect_graph_node_clip"},
    grapple::foundation::NodeId{"node_clip"},
    {
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect"},
        grapple::timeline::EffectPayload{
          "Effect",
          grapple::timeline::EffectImplementation{
            grapple::timeline::EffectImplementationKind::Python,
            "prepare",
            grapple::timeline::EffectSource{
              grapple::timeline::EffectSourceKind::InlineSource,
              "python",
              "def prepare(): pass",
              std::nullopt,
              grapple::foundation::stableHash("def prepare(): pass")
            }
          },
          grapple::timeline::EffectPortSet{},
          grapple::timeline::ParamSet{},
          grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, grapple::foundation::TimeSeconds{10.0}}
        },
        true
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_targets_clip"},
        grapple::foundation::NodeId{"node_effect"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        0,
        true
      }
    }
  });
  return plan;
}

grapple::timeline::EffectPayload makeEffectPayload(std::string name, double targetX) {
  const std::string source = "def prepare(): pass";
  return grapple::timeline::EffectPayload{
    std::move(name),
    grapple::timeline::EffectImplementation{
      grapple::timeline::EffectImplementationKind::Python,
      "prepare",
      grapple::timeline::EffectSource{
        grapple::timeline::EffectSourceKind::InlineSource,
        "python",
        source,
        std::nullopt,
        grapple::foundation::stableHash(source)
      }
    },
    grapple::timeline::EffectPortSet{},
    grapple::timeline::ParamSet{
      {grapple::timeline::Param{"target_x", targetX}}
    },
    grapple::foundation::TimeRange{
      grapple::foundation::TimeSeconds{0.0},
      grapple::foundation::TimeSeconds{10.0}
    }
  };
}

grapple::projection::RenderPlan makeEffectChainPlan(double effectAParam, double playbackRate = 1.0) {
  grapple::projection::RenderPlan plan = makeClipPlan(playbackRate);
  plan.effectGraphs.push_back(grapple::projection::RenderEffectGraph{
    grapple::foundation::GraphId{"effect_graph_node_clip"},
    grapple::foundation::NodeId{"node_clip"},
    {
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_a"},
        makeEffectPayload("Effect A", effectAParam),
        true
      },
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_b"},
        makeEffectPayload("Effect B", 0.2),
        true
      },
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_c"},
        makeEffectPayload("Effect C", 0.3),
        true
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_targets_clip"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        0,
        true
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_b_targets_clip"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        1,
        true
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_c_targets_clip"},
        grapple::foundation::NodeId{"node_effect_c"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        2,
        true
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_to_b"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"output"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"input"},
        3,
        true
      }
    }
  });
  return plan;
}

grapple::projection::RenderPlan makeEffectChainPlanWithAssetVersion(std::string assetVersion) {
  grapple::projection::RenderPlan plan = makeEffectChainPlan(0.1);
  plan.assets[0].versionHash = grapple::foundation::stableHash(assetVersion);
  return plan;
}

grapple::projection::RenderPlan makeEffectChainPlanWithDisabledLink(double effectAParam) {
  grapple::projection::RenderPlan plan = makeEffectChainPlan(effectAParam);
  plan.effectGraphs[0].edges[3].enabled = false;
  return plan;
}

grapple::projection::RenderPlan makeEffectChainPlanWithDisabledEffect() {
  grapple::projection::RenderPlan plan = makeEffectChainPlan(0.1);
  plan.effectGraphs[0].nodes[1].enabled = false;
  return plan;
}

grapple::projection::RenderPlan makeOutOfOrderEffectChainPlan(double effectAParam) {
  grapple::projection::RenderPlan plan = makeClipPlan(1.0);
  plan.effectGraphs.push_back(grapple::projection::RenderEffectGraph{
    grapple::foundation::GraphId{"effect_graph_node_clip"},
    grapple::foundation::NodeId{"node_clip"},
    {
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_b"},
        makeEffectPayload("Effect B", 0.2),
        true
      },
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_a"},
        makeEffectPayload("Effect A", effectAParam),
        true
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_b_targets_clip"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        0,
        true
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_targets_clip"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        1,
        true
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_to_b"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"output"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"input"},
        2,
        true
      }
    }
  });
  return plan;
}

bool containsDependency(
  const std::vector<grapple::runtime::RuntimeDependencyId>& dependencies,
  grapple::runtime::RuntimeDependencyId dependencyId
) {
  for (const grapple::runtime::RuntimeDependencyId& dependency : dependencies) {
    if (dependency == dependencyId) {
      return true;
    }
  }

  return false;
}

} // namespace

int main() {
  using namespace grapple;

  const runtime::RuntimeDependencyPlanner planner;
  const runtime::RuntimeDependencyGraph first = planner.build(makePlan("Video"));
  const runtime::RuntimeDependencyGraph second = planner.build(makePlan("Video"));
  const runtime::RuntimeDependencyGraph changed = planner.build(makePlan("Changed"));
  const runtime::RuntimeDependencyGraph firstClip = planner.build(makeClipPlan(1.0));
  const runtime::RuntimeDependencyGraph changedClip = planner.build(makeClipPlan(2.0));
  const runtime::RuntimeDependencyGraph disabledClip = planner.build(makeDisabledClipPlan());
  const runtime::RuntimeDependencyGraph renamedLayerClip = planner.build(makeClipPlanWithLayerName("Renamed"));
  const runtime::RuntimeDependencyGraph firstEffect = planner.build(makeEffectPlan("def prepare(): pass"));
  const runtime::RuntimeDependencyGraph changedEffect = planner.build(makeEffectPlan("def prepare(): return 1"));
  const runtime::RuntimeDependencyGraph cameraEffect = planner.build(makeEffectPlanWithCameraX(0.0));
  const runtime::RuntimeDependencyGraph clipEffect = planner.build(makeClipEffectPlan());
  const runtime::RuntimeDependencyGraph effectChain = planner.build(makeEffectChainPlan(0.1));

  GRAPPLE_REQUIRE(first.planHash == second.planHash);
  GRAPPLE_REQUIRE(first.projectId == foundation::ProjectId{"proj_runtime"});
  GRAPPLE_REQUIRE(!(first.planHash == changed.planHash));
  GRAPPLE_REQUIRE(firstClip.nodes.size() == 1);
  GRAPPLE_REQUIRE(firstClip.nodes[0].id == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(firstClip.nodes[0].renderNodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(firstClip.nodes[0].assetDependencies.size() == 1);
  GRAPPLE_REQUIRE(firstClip.nodes[0].assetDependencies[0].assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(firstClip.nodes[0].assetDependencies[0].versionHash == foundation::stableHash("asset_video_v1"));
  GRAPPLE_REQUIRE(!(firstClip.nodes[0].paramsHash == changedClip.nodes[0].paramsHash));
  GRAPPLE_REQUIRE(disabledClip.nodes.empty());
  const foundation::TimeRange expectedClipRange{
    foundation::TimeSeconds{0.0},
    foundation::TimeSeconds{10.0}
  };
  GRAPPLE_REQUIRE(firstClip.nodes[0].activeRange == expectedClipRange);
  GRAPPLE_REQUIRE(firstEffect.nodes.size() == 2);
  GRAPPLE_REQUIRE(firstEffect.nodes[0].id == runtime::RuntimeDependencyId{"dep_node_camera"});
  GRAPPLE_REQUIRE(firstEffect.nodes[0].renderNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(firstEffect.nodes[1].id == runtime::RuntimeDependencyId{"dep_node_effect"});
  GRAPPLE_REQUIRE(firstEffect.nodes[1].renderNodeId == foundation::NodeId{"node_effect"});
  GRAPPLE_REQUIRE(firstEffect.nodes[1].inputDependencies.size() == 1);
  GRAPPLE_REQUIRE(firstEffect.nodes[1].inputDependencies[0] == runtime::RuntimeDependencyId{"dep_node_camera"});
  GRAPPLE_REQUIRE(!(firstEffect.planHash == changedEffect.planHash));
  GRAPPLE_REQUIRE(!(firstEffect.nodes[1].implementationHash == changedEffect.nodes[1].implementationHash));
  const runtime::RuntimeInvalidationResult cameraParamInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    cameraEffect,
    makeEffectPlanWithCameraX(24.0),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(cameraParamInvalidation.invalidatedDependencies.size() == 2);
  GRAPPLE_REQUIRE(cameraParamInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_camera"});
  GRAPPLE_REQUIRE(cameraParamInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect"});
  GRAPPLE_REQUIRE(clipEffect.nodes.size() == 2);
  GRAPPLE_REQUIRE(clipEffect.nodes[1].inputDependencies.size() == 1);
  GRAPPLE_REQUIRE(clipEffect.nodes[1].inputDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(effectChain.nodes.size() == 4);
  GRAPPLE_REQUIRE(effectChain.nodes[1].inputDependencies.size() == 1);
  GRAPPLE_REQUIRE(effectChain.nodes[1].inputDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(effectChain.nodes[2].inputDependencies.size() == 2);
  GRAPPLE_REQUIRE(containsDependency(effectChain.nodes[2].inputDependencies, runtime::RuntimeDependencyId{"dep_node_clip"}));
  GRAPPLE_REQUIRE(containsDependency(effectChain.nodes[2].inputDependencies, runtime::RuntimeDependencyId{"dep_node_effect_a"}));
  GRAPPLE_REQUIRE(effectChain.nodes[3].inputDependencies.size() == 1);
  GRAPPLE_REQUIRE(effectChain.nodes[3].inputDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  const runtime::RuntimeCacheKey clipCacheKey = runtime::runtimeCacheKeyForDependency(
    effectChain,
    effectChain.nodes[0],
    "runtime_v1"
  );
  GRAPPLE_REQUIRE(clipCacheKey.projectId == foundation::ProjectId{"proj_runtime"});
  GRAPPLE_REQUIRE(clipCacheKey.nodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(clipCacheKey.assetDependencies.size() == 1);
  GRAPPLE_REQUIRE(clipCacheKey.assetDependencies[0].assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(!(firstClip.planHash == renamedLayerClip.planHash));
  GRAPPLE_REQUIRE(runtime::runtimeCacheKeyForDependency(firstClip, firstClip.nodes[0], "runtime_v1") ==
                  runtime::runtimeCacheKeyForDependency(renamedLayerClip, renamedLayerClip.nodes[0], "runtime_v1"));
  const runtime::RuntimeDependencyGraph changedEffectChain = planner.build(makeEffectChainPlan(0.9));
  const runtime::RuntimeCacheKey effectBCacheKey = runtime::runtimeCacheKeyForDependency(
    effectChain,
    effectChain.nodes[2],
    "runtime_v1"
  );
  const runtime::RuntimeCacheKey changedEffectBCacheKey = runtime::runtimeCacheKeyForDependency(
    changedEffectChain,
    changedEffectChain.nodes[2],
    "runtime_v1"
  );
  GRAPPLE_REQUIRE(!(effectBCacheKey.inputsHash == changedEffectBCacheKey.inputsHash));
  const runtime::RuntimeDependencyGraph disabledLinkEffectChain = planner.build(makeEffectChainPlanWithDisabledLink(0.1));
  GRAPPLE_REQUIRE(disabledLinkEffectChain.nodes[2].inputDependencies.size() == 1);
  GRAPPLE_REQUIRE(disabledLinkEffectChain.nodes[2].inputDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  const runtime::RuntimeDependencyGraph disabledEffectChain = planner.build(makeEffectChainPlanWithDisabledEffect());
  GRAPPLE_REQUIRE(disabledEffectChain.nodes.size() == 3);
  GRAPPLE_REQUIRE(disabledEffectChain.nodes[0].id == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(disabledEffectChain.nodes[1].id == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(disabledEffectChain.nodes[2].id == runtime::RuntimeDependencyId{"dep_node_effect_c"});
  const runtime::RuntimeInvalidationResult effectAParamInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    effectChain,
    makeEffectChainPlan(0.9),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedDependencies.size() == 2);
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedCacheKeys.size() == 2);
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedCacheKeys[0] ==
                  runtime::runtimeCacheKeyForDependency(effectChain, effectChain.nodes[1], "runtime_v1"));
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedCacheKeys[1] ==
                  runtime::runtimeCacheKeyForDependency(effectChain, effectChain.nodes[2], "runtime_v1"));
  const runtime::RuntimeDependencyGraph outOfOrderEffectChain = planner.build(makeOutOfOrderEffectChainPlan(0.1));
  const runtime::RuntimeInvalidationResult outOfOrderEffectInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    outOfOrderEffectChain,
    makeOutOfOrderEffectChainPlan(0.9),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(outOfOrderEffectInvalidation.invalidatedDependencies.size() == 2);
  GRAPPLE_REQUIRE(outOfOrderEffectInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(outOfOrderEffectInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  const runtime::RuntimeInvalidationResult disabledLinkEffectInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    disabledLinkEffectChain,
    makeEffectChainPlanWithDisabledLink(0.9),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(disabledLinkEffectInvalidation.invalidatedDependencies.size() == 1);
  GRAPPLE_REQUIRE(disabledLinkEffectInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  const runtime::RuntimeInvalidationResult clipParamInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    effectChain,
    makeEffectChainPlan(0.1, 2.0),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies.size() == 4);
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[2] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[3] == runtime::RuntimeDependencyId{"dep_node_effect_c"});
  const runtime::RuntimeInvalidationResult assetVersionInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    firstClip,
    makeClipPlanWithAssetVersion("asset_video_v2"),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(assetVersionInvalidation.invalidatedDependencies.size() == 1);
  GRAPPLE_REQUIRE(assetVersionInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(assetVersionInvalidation.invalidatedCacheKeys.size() == 1);
  GRAPPLE_REQUIRE(assetVersionInvalidation.invalidatedCacheKeys[0].assetDependencies[0].versionHash ==
                  foundation::stableHash("asset_video_v1"));
  const runtime::RuntimeInvalidationResult downstreamAssetVersionInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    effectChain,
    makeEffectChainPlanWithAssetVersion("asset_video_v2"),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies.size() == 4);
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[2] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[3] == runtime::RuntimeDependencyId{"dep_node_effect_c"});
  const runtime::RuntimeInvalidationResult removedDependencyInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    firstClip,
    makePlan("Video"),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(removedDependencyInvalidation.invalidatedDependencies.size() == 1);
  GRAPPLE_REQUIRE(removedDependencyInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(removedDependencyInvalidation.invalidatedCacheKeys.size() == 1);
  GRAPPLE_REQUIRE(removedDependencyInvalidation.invalidatedCacheKeys[0] ==
                  runtime::runtimeCacheKeyForDependency(firstClip, firstClip.nodes[0], "runtime_v1"));
  GRAPPLE_REQUIRE(first.nodes.empty());

  const runtime::RuntimeEvaluator evaluator;
  const auto prepared = evaluator.prepare(runtime::PrepareRuntimePlanRequest{
    makePlan("Video")
  });

  GRAPPLE_REQUIRE(prepared);
  GRAPPLE_REQUIRE(prepared.value().prepared.sourceRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(prepared.value().prepared.planHash == first.planHash);
  GRAPPLE_REQUIRE(prepared.value().diagnostics.empty());

  const auto preparedClipPlan = evaluator.prepare(runtime::PrepareRuntimePlanRequest{
    makeClipPlan(1.0)
  });
  GRAPPLE_REQUIRE(preparedClipPlan);
  const auto activeSample = evaluator.sample(runtime::RuntimeSampleRequest{
    preparedClipPlan.value().prepared,
    foundation::TimeSeconds{4.0},
    runtime::RuntimeQuality::Interactive
  });
  GRAPPLE_REQUIRE(activeSample);
  GRAPPLE_REQUIRE(activeSample.value().sample.time == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(activeSample.value().sample.layers.size() == 1);
  GRAPPLE_REQUIRE(activeSample.value().sample.layers[0].sourceNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(activeSample.value().sample.clips.size() == 1);
  GRAPPLE_REQUIRE(activeSample.value().sample.clips[0].sourceNodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(activeSample.value().sample.clips[0].payload.sourceRange.start == foundation::TimeSeconds{1.0});
  const auto inactiveSample = evaluator.sample(runtime::RuntimeSampleRequest{
    preparedClipPlan.value().prepared,
    foundation::TimeSeconds{12.0},
    runtime::RuntimeQuality::Interactive
  });
  GRAPPLE_REQUIRE(inactiveSample);
  GRAPPLE_REQUIRE(inactiveSample.value().sample.layers.size() == 1);
  GRAPPLE_REQUIRE(inactiveSample.value().sample.clips.empty());

  const auto preparedCameraPlan = evaluator.prepare(runtime::PrepareRuntimePlanRequest{
    makeEffectPlan("def prepare(): pass")
  });
  GRAPPLE_REQUIRE(preparedCameraPlan);
  const auto cameraSample = evaluator.sample(runtime::RuntimeSampleRequest{
    preparedCameraPlan.value().prepared,
    foundation::TimeSeconds{2.0},
    runtime::RuntimeQuality::Interactive
  });
  GRAPPLE_REQUIRE(cameraSample);
  GRAPPLE_REQUIRE(cameraSample.value().sample.cameras.size() == 1);
  GRAPPLE_REQUIRE(cameraSample.value().sample.cameras[0].sourceNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(cameraSample.value().diagnostics.empty());
  const auto sampledRange = evaluator.evaluateRange(runtime::RuntimeRangeRequest{
    preparedClipPlan.value().prepared,
    foundation::TimeRange{
      foundation::TimeSeconds{0.0},
      foundation::TimeSeconds{1.0}
    },
    foundation::FrameRate{2, 1},
    runtime::RuntimeQuality::Final
  });
  GRAPPLE_REQUIRE(sampledRange);
  GRAPPLE_REQUIRE(sampledRange.value().range.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(sampledRange.value().range.end == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(sampledRange.value().frames.size() == 2);
  GRAPPLE_REQUIRE(sampledRange.value().frames[0].frame == foundation::FrameNumber{0});
  GRAPPLE_REQUIRE(sampledRange.value().frames[0].sample.time == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(sampledRange.value().frames[0].sample.clips.size() == 1);
  GRAPPLE_REQUIRE(sampledRange.value().frames[1].frame == foundation::FrameNumber{1});
  GRAPPLE_REQUIRE(sampledRange.value().frames[1].sample.time == foundation::TimeSeconds{0.5});
  GRAPPLE_REQUIRE(sampledRange.value().frames[1].sample.clips.size() == 1);
  GRAPPLE_REQUIRE(sampledRange.value().diagnostics.empty());

  runtime::MemoryRuntimeCache cache;
  const runtime::RuntimeCacheKey key{
    foundation::ProjectId{"proj_runtime"},
    foundation::NodeId{"node_track"},
    foundation::stableHash("implementation"),
    foundation::stableHash("params"),
    foundation::stableHash("inputs"),
    {
      runtime::RuntimeAssetDependency{
        foundation::AssetId{"asset_video"},
        foundation::stableHash("asset_v1")
      }
    },
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    "runtime_v1"
  };
  const runtime::RuntimeCacheKey unrelatedKey{
    foundation::ProjectId{"proj_runtime"},
    foundation::NodeId{"node_unrelated"},
    foundation::stableHash("implementation"),
    foundation::stableHash("params"),
    foundation::stableHash("inputs"),
    {},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    "runtime_v1"
  };

  const auto put = cache.put(key, runtime::RuntimeValue{42.0});
  GRAPPLE_REQUIRE(put);
  const auto putUnrelated = cache.put(unrelatedKey, runtime::RuntimeValue{7.0});
  GRAPPLE_REQUIRE(putUnrelated);
  GRAPPLE_REQUIRE(cache.size() == 2);

  const auto cached = cache.get(key);
  GRAPPLE_REQUIRE(cached.has_value());
  GRAPPLE_REQUIRE(std::get<double>(*cached) == 42.0);
  const auto invalidate = cache.invalidate({key});
  GRAPPLE_REQUIRE(invalidate);
  GRAPPLE_REQUIRE(cache.size() == 1);
  GRAPPLE_REQUIRE(!cache.get(key).has_value());
  const auto cachedUnrelated = cache.get(unrelatedKey);
  GRAPPLE_REQUIRE(cachedUnrelated.has_value());
  GRAPPLE_REQUIRE(std::get<double>(*cachedUnrelated) == 7.0);

  return 0;
}
