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
  const runtime::RuntimeDependencyGraph firstEffect = planner.build(makeEffectPlan("def prepare(): pass"));
  const runtime::RuntimeDependencyGraph changedEffect = planner.build(makeEffectPlan("def prepare(): return 1"));
  const runtime::RuntimeDependencyGraph clipEffect = planner.build(makeClipEffectPlan());
  const runtime::RuntimeDependencyGraph effectChain = planner.build(makeEffectChainPlan(0.1));

  GRAPPLE_REQUIRE(first.planHash == second.planHash);
  GRAPPLE_REQUIRE(!(first.planHash == changed.planHash));
  GRAPPLE_REQUIRE(firstClip.nodes.size() == 1);
  GRAPPLE_REQUIRE(firstClip.nodes[0].id == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(firstClip.nodes[0].renderNodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(firstClip.nodes[0].assetDependencies.size() == 1);
  GRAPPLE_REQUIRE(firstClip.nodes[0].assetDependencies[0].assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(firstClip.nodes[0].assetDependencies[0].versionHash == foundation::stableHash("asset_video_v1"));
  GRAPPLE_REQUIRE(!(firstClip.nodes[0].paramsHash == changedClip.nodes[0].paramsHash));
  const foundation::TimeRange expectedClipRange{
    foundation::TimeSeconds{0.0},
    foundation::TimeSeconds{10.0}
  };
  GRAPPLE_REQUIRE(firstClip.nodes[0].activeRange == expectedClipRange);
  GRAPPLE_REQUIRE(firstEffect.nodes.size() == 1);
  GRAPPLE_REQUIRE(firstEffect.nodes[0].id == runtime::RuntimeDependencyId{"dep_node_effect"});
  GRAPPLE_REQUIRE(firstEffect.nodes[0].renderNodeId == foundation::NodeId{"node_effect"});
  GRAPPLE_REQUIRE(firstEffect.nodes[0].inputDependencies.empty());
  GRAPPLE_REQUIRE(!(firstEffect.planHash == changedEffect.planHash));
  GRAPPLE_REQUIRE(!(firstEffect.nodes[0].implementationHash == changedEffect.nodes[0].implementationHash));
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
  const runtime::RuntimeInvalidationResult effectAParamInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    effectChain,
    makeEffectChainPlan(0.9)
  });
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedDependencies.size() == 2);
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(effectAParamInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  const runtime::RuntimeInvalidationResult clipParamInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    effectChain,
    makeEffectChainPlan(0.1, 2.0)
  });
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies.size() == 4);
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[2] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  GRAPPLE_REQUIRE(clipParamInvalidation.invalidatedDependencies[3] == runtime::RuntimeDependencyId{"dep_node_effect_c"});
  const runtime::RuntimeInvalidationResult assetVersionInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    firstClip,
    makeClipPlanWithAssetVersion("asset_video_v2")
  });
  GRAPPLE_REQUIRE(assetVersionInvalidation.invalidatedDependencies.size() == 1);
  GRAPPLE_REQUIRE(assetVersionInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  const runtime::RuntimeInvalidationResult downstreamAssetVersionInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    effectChain,
    makeEffectChainPlanWithAssetVersion("asset_video_v2")
  });
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies.size() == 4);
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[2] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  GRAPPLE_REQUIRE(downstreamAssetVersionInvalidation.invalidatedDependencies[3] == runtime::RuntimeDependencyId{"dep_node_effect_c"});
  GRAPPLE_REQUIRE(first.nodes.empty());

  const runtime::RuntimeEvaluator evaluator;
  const auto prepared = evaluator.prepare(runtime::PrepareRuntimePlanRequest{
    makePlan("Video"),
    runtime::RuntimePrepareMode::Interactive
  });

  GRAPPLE_REQUIRE(prepared);
  GRAPPLE_REQUIRE(prepared.value().prepared.sourceRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(prepared.value().prepared.planHash == first.planHash);
  GRAPPLE_REQUIRE(prepared.value().diagnostics.empty());

  const auto preparedClipPlan = evaluator.prepare(runtime::PrepareRuntimePlanRequest{
    makeClipPlan(1.0),
    runtime::RuntimePrepareMode::Interactive
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
    makeEffectPlan("def prepare(): pass"),
    runtime::RuntimePrepareMode::Interactive
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

  runtime::MemoryRuntimeCache cache;
  const runtime::RuntimeCacheKey key{
    first.planHash,
    foundation::NodeId{"node_track"},
    foundation::stableHash("implementation"),
    foundation::stableHash("params"),
    foundation::stableHash("inputs"),
    foundation::AssetId{"asset_video"},
    foundation::stableHash("asset_v1"),
    std::nullopt,
    "",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    "runtime_v1"
  };
  const runtime::RuntimeCacheKey unrelatedKey{
    first.planHash,
    foundation::NodeId{"node_unrelated"},
    foundation::stableHash("implementation"),
    foundation::stableHash("params"),
    foundation::stableHash("inputs"),
    std::nullopt,
    foundation::Hash256{},
    std::nullopt,
    "",
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
