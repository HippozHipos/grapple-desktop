#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

grapple::storage::ProjectCommitRecordOptions commandOptions() {
  return grapple::storage::ProjectCommitRecordOptions{
    std::chrono::system_clock::now(),
    std::nullopt
  };
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

  app::NativeProjectSession session{
    foundation::ProjectId{"proj_cli"},
    "CLI Smoke Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_cli"},
      foundation::FilePath{"cli.grapple"},
      1
    }
  };

  const auto initialSnapshot = session.snapshot();
  if (!initialSnapshot) {
    printError(initialSnapshot.error());
    return 1;
  }

  const auto registeredAsset = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_register_asset"},
      foundation::ProjectId{"proj_cli"},
      initialSnapshot.value().revision,
      project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "cli"},
      project::RegisterAssetCommand{
        asset::Asset{
          foundation::AssetId{"asset_video"},
          "Walking Woman",
          asset::AssetMetadata{
            asset::AssetMediaType::Video,
            foundation::FilePath{"/media/walking-woman.mp4"},
            std::nullopt,
            foundation::TimeSeconds{10.0},
            foundation::Resolution{1080, 1920},
            foundation::FrameRate{30, 1}
          }
        }
      }
    },
    commandOptions()
  );
  if (!registeredAsset) {
    printError(registeredAsset.error());
    return 1;
  }

  const auto composition = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_composition"},
      foundation::ProjectId{"proj_cli"},
      registeredAsset.value().commandResult.afterRevision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "cli"},
      project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
    },
    commandOptions()
  );
  if (!composition) {
    printError(composition.error());
    return 1;
  }

  const auto track = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_track"},
      foundation::ProjectId{"proj_cli"},
      composition.value().commandResult.afterRevision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "cli"},
      project::CreateTrackCommand{
        foundation::NodeId{"node_track"},
        foundation::NodeId{"node_composition"},
        foundation::EdgeId{"edge_contains_track"},
        "Video"
      }
    },
    commandOptions()
  );
  if (!track) {
    printError(track.error());
    return 1;
  }

  const auto clip = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_clip"},
      foundation::ProjectId{"proj_cli"},
      track.value().commandResult.afterRevision,
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
    },
    commandOptions()
  );
  if (!clip) {
    printError(clip.error());
    return 1;
  }

  const auto camera = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_camera"},
      foundation::ProjectId{"proj_cli"},
      clip.value().commandResult.afterRevision,
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
    },
    commandOptions()
  );
  if (!camera) {
    printError(camera.error());
    return 1;
  }

  const std::string effectSource = "def prepare(ctx):\n  return {'camera': ctx.time}\n";
  const auto effect = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_effect"},
      foundation::ProjectId{"proj_cli"},
      camera.value().commandResult.afterRevision,
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
        },
        graph::PortName{"camera"},
        graph::PortName{"input"},
        0
      }
    },
    commandOptions()
  );
  if (!effect) {
    printError(effect.error());
    return 1;
  }

  const auto renderPlan = session.buildRenderPlan();
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
  std::cout << "duration=" << renderPlan.value().plan.duration.value << '\n';
  std::cout << "layers=" << renderPlan.value().plan.layers.size() << '\n';
  std::cout << "clips=" << renderPlan.value().plan.clips.size() << '\n';
  std::cout << "cameras=" << renderPlan.value().plan.cameras.size() << '\n';
  std::cout << "effectGraphs=" << renderPlan.value().plan.effectGraphs.size() << '\n';
  std::cout << "diagnostics=" << renderPlan.value().diagnostics.size() << '\n';

  return 0;
}
