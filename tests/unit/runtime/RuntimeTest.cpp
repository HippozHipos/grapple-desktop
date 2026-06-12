#include <grapple/runtime/RuntimeDependencyPlanner.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>

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

} // namespace

int main() {
  using namespace grapple;

  const runtime::RuntimeDependencyPlanner planner;
  const runtime::RuntimeDependencyGraph first = planner.build(makePlan("Video"));
  const runtime::RuntimeDependencyGraph second = planner.build(makePlan("Video"));
  const runtime::RuntimeDependencyGraph changed = planner.build(makePlan("Changed"));

  GRAPPLE_REQUIRE(first.planHash == second.planHash);
  GRAPPLE_REQUIRE(!(first.planHash == changed.planHash));
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

  return 0;
}

