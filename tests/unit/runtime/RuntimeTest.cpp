#include <grapple/runtime/RuntimeDependencyPlanner.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/runtime/MemoryRuntimeCache.hpp>

#include <TestAssert.hpp>

namespace {

grapple::projection::RenderPlan makePlan(std::string layerName) {
  return grapple::projection::RenderPlan{
    grapple::foundation::ProjectId{"proj_runtime"},
    grapple::foundation::RevisionId{"rev_4"},
    grapple::projection::RenderStage{"Runtime Test"},
    grapple::foundation::TimeSeconds{10.0},
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
        grapple::foundation::NodeId{"node_camera"},
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
        grapple::foundation::NodeId{"node_clip"},
        true
      }
    }
  });
  return plan;
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

  GRAPPLE_REQUIRE(first.planHash == second.planHash);
  GRAPPLE_REQUIRE(!(first.planHash == changed.planHash));
  GRAPPLE_REQUIRE(firstClip.nodes.size() == 1);
  GRAPPLE_REQUIRE(firstClip.nodes[0].id == runtime::RuntimeDependencyId{"dep_node_clip"});
  GRAPPLE_REQUIRE(firstClip.nodes[0].renderNodeId == foundation::NodeId{"node_clip"});
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

  const auto put = cache.put(key, runtime::RuntimeValue{42.0});
  GRAPPLE_REQUIRE(put);
  GRAPPLE_REQUIRE(cache.size() == 1);

  const auto cached = cache.get(key);
  GRAPPLE_REQUIRE(cached.has_value());
  GRAPPLE_REQUIRE(std::get<double>(*cached) == 42.0);

  return 0;
}
