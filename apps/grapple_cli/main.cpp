#include <grapple/project/ProjectController.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <iostream>
#include <optional>
#include <string>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
  using namespace grapple;

  bool printRenderPlanJson = false;
  if (argc == 2) {
    const std::string argument{argv[1]};
    if (argument != "--render-plan-json") {
      std::cerr << "Unknown argument: " << argument << '\n';
      return 1;
    }
    printRenderPlanJson = true;
  } else if (argc > 2) {
    std::cerr << "Expected zero arguments or --render-plan-json.\n";
    return 1;
  }

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

  if (printRenderPlanJson) {
    std::cout << projection::serializeCanonicalRenderPlan(renderPlan.value().plan) << '\n';
    return 0;
  }

  std::cout << "project=" << renderPlan.value().plan.projectId.value() << '\n';
  std::cout << "revision=" << renderPlan.value().plan.revision.value() << '\n';
  std::cout << "layers=" << renderPlan.value().plan.layers.size() << '\n';
  std::cout << "diagnostics=" << renderPlan.value().diagnostics.size() << '\n';

  return 0;
}
