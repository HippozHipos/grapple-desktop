#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/projection/RenderPlanBuilder.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/projection/TimelineProjector.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

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

  const auto clip = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_clip"},
    foundation::ProjectId{"proj_cli"},
    track.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "cli"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip"},
      foundation::NodeId{"node_track"},
      foundation::EdgeId{"edge_contains_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
        1.0,
        foundation::AssetId{"asset_video"},
        timeline::Transform{}
      }
    }
  });
  if (!clip) {
    printError(clip.error());
    return 1;
  }

  const auto camera = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_camera"},
    foundation::ProjectId{"proj_cli"},
    clip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "cli"},
    project::CreateCameraCommand{
      foundation::NodeId{"node_camera"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_camera"},
      timeline::CameraPayload{
        "Camera",
        timeline::Transform{},
        timeline::CameraLens{35.0}
      }
    }
  });
  if (!camera) {
    printError(camera.error());
    return 1;
  }

  const std::string effectSource = "def prepare(ctx):\n  return {'camera': ctx.time}\n";
  const auto effect = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_effect"},
    foundation::ProjectId{"proj_cli"},
    camera.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_cli"}, "cli-agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_effect"},
      foundation::NodeId{"node_camera"},
      foundation::EdgeId{"edge_effect_targets_camera"},
      timeline::EffectPayload{
        "Camera Follow",
        timeline::EffectImplementation{
          timeline::EffectImplementationKind::Python,
          "prepare",
          timeline::EffectSource{
            timeline::EffectSourceKind::InlineSource,
            "python",
            effectSource,
            std::nullopt,
            foundation::stableHash(effectSource)
          }
        },
        timeline::EffectPortSet{
          {timeline::EffectPort{"frame"}},
          {timeline::EffectPort{"camera"}}
        },
        timeline::ParamSet{
          {timeline::Param{"target_x", 0.5}}
        },
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}}
      }
    }
  });
  if (!effect) {
    printError(effect.error());
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
  std::cout << "clips=" << renderPlan.value().plan.clips.size() << '\n';
  std::cout << "cameras=" << renderPlan.value().plan.cameras.size() << '\n';
  std::cout << "effectGraphs=" << renderPlan.value().plan.effectGraphs.size() << '\n';
  std::cout << "diagnostics=" << renderPlan.value().diagnostics.size() << '\n';

  return 0;
}
