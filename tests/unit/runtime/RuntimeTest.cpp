#include <grapple/runtime/BuiltinEffectRuntime.hpp>
#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/runtime/RuntimeDependencyPlanner.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/runtime/MemoryRuntimeCache.hpp>
#include <grapple/runtime/RuntimeOutputNames.hpp>

#include <TestAssert.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class TestEffectRuntime final : public grapple::runtime::IEffectRuntime {
public:
  bool supports(const grapple::projection::RenderEffectNode& node) const override {
    return node.payload.implementation.kind == grapple::timeline::EffectImplementationKind::Python;
  }

  grapple::foundation::Result<grapple::runtime::EffectPrepareResult> prepare(
    const grapple::runtime::EffectPrepareRequest& request
  ) override {
    ++prepareCount;
    return grapple::runtime::EffectPrepareResult{
      grapple::runtime::PreparedEffectNode{
        request.graph.id,
        request.graph.targetNodeId,
        request.node.sourceNodeId,
        nullptr,
        {
          grapple::runtime::RuntimeNamedValue{
            "prepared",
            grapple::runtime::RuntimeValue{true}
          }
        }
      },
      {}
    };
  }

  grapple::foundation::Result<grapple::runtime::EffectProcessResult> process(
    const grapple::runtime::EffectProcessRequest& request
  ) override {
    ++processCount;
    return grapple::runtime::EffectProcessResult{
      grapple::runtime::RuntimeEffectOutput{
        request.prepared.effectGraphId,
        request.prepared.targetNodeId,
        request.prepared.sourceNodeId,
        {
          grapple::runtime::RuntimeNamedValue{
            "time",
            grapple::runtime::RuntimeValue{request.time.value}
          }
        }
      },
      {}
    };
  }

  int prepareCount = 0;
  int processCount = 0;
};

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
        std::move(layerName)
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
    }
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

grapple::projection::RenderPlan makeEffectPlan(std::string source) {
  grapple::projection::RenderPlan plan = makePlan("Video");
  plan.cameras.push_back(grapple::projection::RenderCamera{
    grapple::foundation::NodeId{"node_camera"},
    "Camera",
    grapple::timeline::Transform{},
    grapple::timeline::CameraLens{}
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
        }
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_targets_camera"},
        grapple::foundation::NodeId{"node_effect"},
        grapple::graph::PortName{"camera_transform"},
        grapple::foundation::NodeId{"node_camera"},
        grapple::graph::PortName{"input"},
        0
      }
    }
  });
  return plan;
}

grapple::projection::RenderPlan makeBuiltinCameraTransformPlan(bool includePositionY = true) {
  grapple::projection::RenderPlan plan = makePlan("Video");
  plan.cameras.push_back(grapple::projection::RenderCamera{
    grapple::foundation::NodeId{"node_camera"},
    "Camera",
    grapple::timeline::Transform{},
    grapple::timeline::CameraLens{}
  });
  grapple::timeline::ParamSet params{
    {grapple::timeline::Param{grapple::runtime::builtin_effect::PositionXParam, 0.25}}
  };
  if (includePositionY) {
    params.values.push_back(grapple::timeline::Param{grapple::runtime::builtin_effect::PositionYParam, -0.5});
  }
  grapple::timeline::EffectPayload payload{
    grapple::runtime::builtin_effect::CameraTransformDisplayName,
    grapple::timeline::EffectImplementation{
      grapple::timeline::EffectImplementationKind::Builtin,
      grapple::runtime::builtin_effect::CameraTransformEntrypoint,
      grapple::timeline::EffectSource{
        grapple::timeline::EffectSourceKind::InlineSource,
        "builtin",
        grapple::runtime::builtin_effect::CameraTransformSource,
        std::nullopt,
        grapple::foundation::stableHash(grapple::runtime::builtin_effect::CameraTransformSource)
      }
    },
    grapple::timeline::EffectPortSet{
      {grapple::timeline::EffectPort{"frame"}},
      {grapple::timeline::EffectPort{grapple::runtime::output_name::CameraTransform}}
    },
    std::move(params),
    grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, grapple::foundation::TimeSeconds{10.0}}
  };
  plan.effectGraphs.push_back(grapple::projection::RenderEffectGraph{
    grapple::foundation::GraphId{"effect_graph_node_camera"},
    grapple::foundation::NodeId{"node_camera"},
    {
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_builtin_effect"},
        std::move(payload)
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_builtin_effect_targets_camera"},
        grapple::foundation::NodeId{"node_builtin_effect"},
        grapple::graph::PortName{grapple::runtime::output_name::CameraTransform},
        grapple::foundation::NodeId{"node_camera"},
        grapple::graph::PortName{"input"},
        0
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
        }
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_targets_clip"},
        grapple::foundation::NodeId{"node_effect"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        0
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
        makeEffectPayload("Effect A", effectAParam)
      },
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_b"},
        makeEffectPayload("Effect B", 0.2)
      },
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_c"},
        makeEffectPayload("Effect C", 0.3)
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_targets_clip"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        0
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_b_targets_clip"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        1
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_c_targets_clip"},
        grapple::foundation::NodeId{"node_effect_c"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        2
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_to_b"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"output"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"input"},
        3
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

grapple::projection::RenderPlan makeEffectChainPlanWithModelVersion(std::string modelVersion) {
  grapple::projection::RenderPlan plan = makeEffectChainPlan(0.1);
  plan.effectGraphs[0].nodes[0].payload.modelDependencies.push_back(grapple::timeline::EffectModelDependency{
    grapple::foundation::ModelId{"model_segmenter"},
    grapple::foundation::stableHash(modelVersion)
  });
  return plan;
}

grapple::projection::RenderPlan makeEffectChainPlanWithUnsortedModels() {
  grapple::projection::RenderPlan plan = makeEffectChainPlan(0.1);
  plan.effectGraphs[0].nodes[0].payload.modelDependencies = {
    grapple::timeline::EffectModelDependency{
      grapple::foundation::ModelId{"model_z"},
      grapple::foundation::stableHash("model_z_v1")
    },
    grapple::timeline::EffectModelDependency{
      grapple::foundation::ModelId{"model_a"},
      grapple::foundation::stableHash("model_a_v1")
    }
  };
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
        makeEffectPayload("Effect B", 0.2)
      },
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_effect_a"},
        makeEffectPayload("Effect A", effectAParam)
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_b_targets_clip"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        0
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_targets_clip"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"frame"},
        grapple::foundation::NodeId{"node_clip"},
        grapple::graph::PortName{"input"},
        1
      },
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_effect_a_to_b"},
        grapple::foundation::NodeId{"node_effect_a"},
        grapple::graph::PortName{"output"},
        grapple::foundation::NodeId{"node_effect_b"},
        grapple::graph::PortName{"input"},
        2
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
  GRAPPLE_REQUIRE(clipCacheKey.modelDependencies.empty());
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
  const runtime::RuntimeDependencyGraph modelEffectChain = planner.build(makeEffectChainPlanWithModelVersion("segmenter_v1"));
  const runtime::RuntimeCacheKey modelEffectCacheKey = runtime::runtimeCacheKeyForDependency(
    modelEffectChain,
    modelEffectChain.nodes[1],
    "runtime_v1"
  );
  GRAPPLE_REQUIRE(modelEffectChain.nodes[1].modelDependencies.size() == 1);
  GRAPPLE_REQUIRE(modelEffectChain.nodes[1].modelDependencies[0].modelId == foundation::ModelId{"model_segmenter"});
  GRAPPLE_REQUIRE(modelEffectCacheKey.modelDependencies.size() == 1);
  GRAPPLE_REQUIRE(modelEffectCacheKey.modelDependencies[0].versionHash == foundation::stableHash("segmenter_v1"));
  const runtime::RuntimeDependencyGraph unsortedModelEffectChain = planner.build(makeEffectChainPlanWithUnsortedModels());
  GRAPPLE_REQUIRE(unsortedModelEffectChain.nodes[1].modelDependencies.size() == 2);
  GRAPPLE_REQUIRE(unsortedModelEffectChain.nodes[1].modelDependencies[0].modelId == foundation::ModelId{"model_a"});
  GRAPPLE_REQUIRE(unsortedModelEffectChain.nodes[1].modelDependencies[1].modelId == foundation::ModelId{"model_z"});
  const runtime::RuntimeInvalidationResult modelVersionInvalidation = planner.diff(runtime::RuntimeInvalidationRequest{
    modelEffectChain,
    makeEffectChainPlanWithModelVersion("segmenter_v2"),
    "runtime_v1"
  });
  GRAPPLE_REQUIRE(modelVersionInvalidation.invalidatedDependencies.size() == 2);
  GRAPPLE_REQUIRE(modelVersionInvalidation.invalidatedDependencies[0] == runtime::RuntimeDependencyId{"dep_node_effect_a"});
  GRAPPLE_REQUIRE(modelVersionInvalidation.invalidatedDependencies[1] == runtime::RuntimeDependencyId{"dep_node_effect_b"});
  GRAPPLE_REQUIRE(modelVersionInvalidation.invalidatedCacheKeys.size() == 2);
  GRAPPLE_REQUIRE(modelVersionInvalidation.invalidatedCacheKeys[0].modelDependencies[0].versionHash ==
                  foundation::stableHash("segmenter_v1"));
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
  GRAPPLE_REQUIRE(preparedCameraPlan.value().prepared.preparedEffects.empty());
  GRAPPLE_REQUIRE(preparedCameraPlan.value().diagnostics.size() == 1);
  GRAPPLE_REQUIRE(preparedCameraPlan.value().diagnostics[0].code == "runtime.effect_runtime_missing");
  GRAPPLE_REQUIRE(preparedCameraPlan.value().diagnostics[0].location.nodeId == foundation::NodeId{"node_effect"});
  const auto cameraSample = evaluator.sample(runtime::RuntimeSampleRequest{
    preparedCameraPlan.value().prepared,
    foundation::TimeSeconds{2.0},
    runtime::RuntimeQuality::Interactive
  });
  GRAPPLE_REQUIRE(cameraSample);
  GRAPPLE_REQUIRE(cameraSample.value().sample.cameras.size() == 1);
  GRAPPLE_REQUIRE(cameraSample.value().sample.cameras[0].sourceNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(cameraSample.value().diagnostics.size() == 1);

  TestEffectRuntime effectRuntime;
  const runtime::RuntimeEvaluator evaluatorWithEffectRuntime{{&effectRuntime}};
  const auto preparedSupportedEffectPlan = evaluatorWithEffectRuntime.prepare(runtime::PrepareRuntimePlanRequest{
    makeEffectPlan("def prepare(): pass")
  });
  GRAPPLE_REQUIRE(preparedSupportedEffectPlan);
  GRAPPLE_REQUIRE(preparedSupportedEffectPlan.value().diagnostics.empty());
  GRAPPLE_REQUIRE(preparedSupportedEffectPlan.value().prepared.preparedEffects.size() == 1);
  GRAPPLE_REQUIRE(preparedSupportedEffectPlan.value().prepared.preparedEffects[0].effectGraphId == foundation::GraphId{"effect_graph_node_camera"});
  GRAPPLE_REQUIRE(preparedSupportedEffectPlan.value().prepared.preparedEffects[0].targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(preparedSupportedEffectPlan.value().prepared.preparedEffects[0].sourceNodeId == foundation::NodeId{"node_effect"});
  GRAPPLE_REQUIRE(preparedSupportedEffectPlan.value().prepared.preparedEffects[0].preparedValues.size() == 1);
  GRAPPLE_REQUIRE(std::get<bool>(preparedSupportedEffectPlan.value().prepared.preparedEffects[0].preparedValues[0].value));
  GRAPPLE_REQUIRE(effectRuntime.prepareCount == 1);
  const auto supportedEffectSample = evaluatorWithEffectRuntime.sample(runtime::RuntimeSampleRequest{
    preparedSupportedEffectPlan.value().prepared,
    foundation::TimeSeconds{3.5},
    runtime::RuntimeQuality::Interactive
  });
  GRAPPLE_REQUIRE(supportedEffectSample);
  GRAPPLE_REQUIRE(supportedEffectSample.value().diagnostics.empty());
  GRAPPLE_REQUIRE(supportedEffectSample.value().sample.effectOutputs.size() == 1);
  GRAPPLE_REQUIRE(supportedEffectSample.value().sample.effectOutputs[0].effectGraphId == foundation::GraphId{"effect_graph_node_camera"});
  GRAPPLE_REQUIRE(supportedEffectSample.value().sample.effectOutputs[0].targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(supportedEffectSample.value().sample.effectOutputs[0].sourceNodeId == foundation::NodeId{"node_effect"});
  GRAPPLE_REQUIRE(supportedEffectSample.value().sample.effectOutputs[0].values.size() == 1);
  GRAPPLE_REQUIRE(std::get<double>(supportedEffectSample.value().sample.effectOutputs[0].values[0].value) == 3.5);
  GRAPPLE_REQUIRE(effectRuntime.processCount == 1);

  runtime::BuiltinEffectRuntime builtinRuntime;
  const runtime::RuntimeEvaluator evaluatorWithBuiltinRuntime{{&builtinRuntime}};
  const auto preparedBuiltinEffectPlan = evaluatorWithBuiltinRuntime.prepare(runtime::PrepareRuntimePlanRequest{
    makeBuiltinCameraTransformPlan()
  });
  GRAPPLE_REQUIRE(preparedBuiltinEffectPlan);
  GRAPPLE_REQUIRE(preparedBuiltinEffectPlan.value().diagnostics.empty());
  GRAPPLE_REQUIRE(preparedBuiltinEffectPlan.value().prepared.preparedEffects.size() == 1);
  const auto builtinEffectSample = evaluatorWithBuiltinRuntime.sample(runtime::RuntimeSampleRequest{
    preparedBuiltinEffectPlan.value().prepared,
    foundation::TimeSeconds{1.0},
    runtime::RuntimeQuality::Interactive
  });
  GRAPPLE_REQUIRE(builtinEffectSample);
  GRAPPLE_REQUIRE(builtinEffectSample.value().diagnostics.empty());
  GRAPPLE_REQUIRE(builtinEffectSample.value().sample.effectOutputs.size() == 1);
  GRAPPLE_REQUIRE(builtinEffectSample.value().sample.effectOutputs[0].targetNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(builtinEffectSample.value().sample.effectOutputs[0].values.size() == 1);
  GRAPPLE_REQUIRE(builtinEffectSample.value().sample.effectOutputs[0].values[0].name == runtime::output_name::CameraTransform);
  const auto* transform = std::get_if<foundation::Transform2D>(&builtinEffectSample.value().sample.effectOutputs[0].values[0].value);
  GRAPPLE_REQUIRE(transform != nullptr);
  GRAPPLE_REQUIRE(transform->position.x == 0.25);
  GRAPPLE_REQUIRE(transform->position.y == -0.5);

  const auto preparedInvalidBuiltinEffectPlan = evaluatorWithBuiltinRuntime.prepare(runtime::PrepareRuntimePlanRequest{
    makeBuiltinCameraTransformPlan(false)
  });
  GRAPPLE_REQUIRE(preparedInvalidBuiltinEffectPlan);
  GRAPPLE_REQUIRE(preparedInvalidBuiltinEffectPlan.value().diagnostics.size() == 1);
  GRAPPLE_REQUIRE(preparedInvalidBuiltinEffectPlan.value().diagnostics[0].code == "runtime.builtin_camera_transform_param_invalid");
  GRAPPLE_REQUIRE(preparedInvalidBuiltinEffectPlan.value().diagnostics[0].location.projectId == foundation::ProjectId{"proj_runtime"});
  GRAPPLE_REQUIRE(preparedInvalidBuiltinEffectPlan.value().diagnostics[0].location.revision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(preparedInvalidBuiltinEffectPlan.value().diagnostics[0].location.nodeId == foundation::NodeId{"node_builtin_effect"});
  GRAPPLE_REQUIRE(preparedInvalidBuiltinEffectPlan.value().prepared.preparedEffects.size() == 1);
  const auto invalidBuiltinSample = evaluatorWithBuiltinRuntime.sample(runtime::RuntimeSampleRequest{
    preparedInvalidBuiltinEffectPlan.value().prepared,
    foundation::TimeSeconds{1.0},
    runtime::RuntimeQuality::Interactive
  });
  GRAPPLE_REQUIRE(invalidBuiltinSample);
  GRAPPLE_REQUIRE(invalidBuiltinSample.value().sample.effectOutputs.size() == 1);
  GRAPPLE_REQUIRE(invalidBuiltinSample.value().sample.effectOutputs[0].values.empty());
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

  runtime::MemoryRuntimeCache cache{2};
  GRAPPLE_REQUIRE(cache.capacity() == 2);
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
    {},
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

  const auto putAgain = cache.put(key, runtime::RuntimeValue{42.0});
  GRAPPLE_REQUIRE(putAgain);
  GRAPPLE_REQUIRE(cache.size() == 2);
  GRAPPLE_REQUIRE(cache.get(unrelatedKey).has_value());
  const runtime::RuntimeCacheKey thirdKey{
    foundation::ProjectId{"proj_runtime"},
    foundation::NodeId{"node_third"},
    foundation::stableHash("implementation"),
    foundation::stableHash("params"),
    foundation::stableHash("inputs"),
    {},
    {},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    "runtime_v1"
  };
  const auto putThird = cache.put(thirdKey, runtime::RuntimeValue{9.0});
  GRAPPLE_REQUIRE(putThird);
  GRAPPLE_REQUIRE(cache.size() == 2);
  GRAPPLE_REQUIRE(!cache.get(key).has_value());
  GRAPPLE_REQUIRE(cache.get(unrelatedKey).has_value());
  const auto thirdCached = cache.get(thirdKey);
  GRAPPLE_REQUIRE(thirdCached.has_value());
  GRAPPLE_REQUIRE(std::get<double>(*thirdCached) == 9.0);

  runtime::MemoryRuntimeCache zeroCapacityCache{0};
  const auto zeroCapacityPut = zeroCapacityCache.put(key, runtime::RuntimeValue{1.0});
  GRAPPLE_REQUIRE(!zeroCapacityPut);
  GRAPPLE_REQUIRE(zeroCapacityPut.error().code == "runtime.cache_capacity_empty");

  return 0;
}
