#include <grapple/project/ProjectController.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <TestAssert.hpp>

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

  const auto snapshot = controller.snapshot();
  GRAPPLE_REQUIRE(snapshot);

  const projection::TimelineProjector projector;
  const auto timelineResult = projector.buildTimelineIR(projection::BuildTimelineIRRequest{
    snapshot.value()
  });
  GRAPPLE_REQUIRE(timelineResult);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.projectId == foundation::ProjectId{"proj_projection"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.revision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(timelineResult.value().timeline.layers.size() == 1);
  GRAPPLE_REQUIRE(timelineResult.value().timeline.layers[0].sourceNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(timelineResult.value().diagnostics.empty());

  const projection::RenderPlanBuilder builder;
  const auto planResult = builder.buildRenderPlan(projection::BuildRenderPlanRequest{
    timelineResult.value().timeline
  });
  GRAPPLE_REQUIRE(planResult);
  GRAPPLE_REQUIRE(planResult.value().plan.projectId == foundation::ProjectId{"proj_projection"});
  GRAPPLE_REQUIRE(planResult.value().plan.revision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(planResult.value().plan.layers.size() == 1);
  GRAPPLE_REQUIRE(planResult.value().plan.layers[0].sourceNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(planResult.value().diagnostics.empty());

  return 0;
}

