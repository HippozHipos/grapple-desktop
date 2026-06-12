#include <grapple/project/ProjectController.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <iostream>
#include <optional>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

} // namespace

int main() {
  using namespace grapple;

  project::ProjectController controller{
    project::createEmptyProject(foundation::ProjectId{"proj_cli"}, "CLI Smoke Project")
  };

  const auto initialSnapshot = controller.snapshot();
  if (!initialSnapshot) {
    printError(initialSnapshot.error());
    return 1;
  }

  const auto composition = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_composition"},
    project::CommandKind::CreateComposition,
    foundation::ProjectId{"proj_cli"},
    initialSnapshot.value().document.revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "cli"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  });
  if (!composition) {
    printError(composition.error());
    return 1;
  }

  const auto track = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_track"},
    project::CommandKind::CreateTrack,
    foundation::ProjectId{"proj_cli"},
    composition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "cli"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_track"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_track"},
      "Video"
    }
  });
  if (!track) {
    printError(track.error());
    return 1;
  }

  const auto snapshot = controller.snapshot();
  if (!snapshot) {
    printError(snapshot.error());
    return 1;
  }

  const projection::TimelineProjector projector;
  const auto timeline = projector.buildTimelineIR(projection::BuildTimelineIRRequest{snapshot.value()});
  if (!timeline) {
    printError(timeline.error());
    return 1;
  }

  const projection::RenderPlanBuilder builder;
  const auto renderPlan = builder.buildRenderPlan(
    projection::BuildRenderPlanRequest{timeline.value().timeline}
  );
  if (!renderPlan) {
    printError(renderPlan.error());
    return 1;
  }

  std::cout << "project=" << renderPlan.value().plan.projectId.value() << '\n';
  std::cout << "revision=" << renderPlan.value().plan.revision.value() << '\n';
  std::cout << "layers=" << renderPlan.value().plan.layers.size() << '\n';
  std::cout << "diagnostics=" << renderPlan.value().diagnostics.size() << '\n';

  return 0;
}

