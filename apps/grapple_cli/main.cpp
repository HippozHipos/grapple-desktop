#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativePreviewSession.hpp>
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
#include <utility>

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

grapple::storage::ProjectCommitRecordOptions commandOptions(grapple::storage::SnapshotCommitRecord snapshot) {
  return grapple::storage::ProjectCommitRecordOptions{
    std::chrono::system_clock::now(),
    std::move(snapshot)
  };
}

} // namespace

int main(int argc, char* argv[]) {
  using namespace grapple;

  bool printRenderPlanJson = false;
  bool printPreviewFrame = false;
  bool runExportSmoke = false;
  bool savePackage = false;
  if (argc == 2) {
    const std::string argument{argv[1]};
    if (argument == "--render-plan-json") {
      printRenderPlanJson = true;
    } else if (argument == "--preview-frame") {
      printPreviewFrame = true;
    } else if (argument == "--export-smoke") {
      runExportSmoke = true;
    } else if (argument == "--save-package") {
      savePackage = true;
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return 1;
    }
  } else if (argc > 2) {
    std::cerr << "Expected zero arguments, --render-plan-json, --preview-frame, --export-smoke, or --save-package.\n";
    return 1;
  }

  app::NativeProjectSession session{
    foundation::ProjectId{"proj_cli"},
    "CLI Smoke Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_cli"},
      foundation::FilePath{savePackage ? "/tmp/grapple-cli-package" : "cli.grapple"},
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
    savePackage
      ? commandOptions(storage::SnapshotCommitRecord{
          foundation::SnapshotId{"snap_cli_rev_6"},
          foundation::FilePath{"snapshots/rev_6.json"},
          std::optional<std::string>{"cli"}
        })
      : commandOptions()
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

  if (printPreviewFrame) {
    app::NativePreviewSession preview{session};
    const auto refresh = preview.refreshFromProject();
    if (!refresh) {
      printError(refresh.error());
      return 1;
    }

    const auto frame = preview.renderFrame(render::RenderFrameRequest{
      foundation::TimeSeconds{0.0},
      render::RenderQuality::Draft
    });
    if (!frame) {
      printError(frame.error());
      return 1;
    }

    std::cout << "revision=" << refresh.value().revision.value() << '\n';
    std::cout << "frame=" << frame.value().frame.description << '\n';
    return 0;
  }

  if (runExportSmoke) {
    app::NativeExportSession exportSession{session};
    const auto prepare = exportSession.prepareFromProject();
    if (!prepare) {
      printError(prepare.error());
      return 1;
    }

    const auto result = exportSession.render(render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::FrameRate{2, 1},
      foundation::Resolution{1920, 1080},
      render::Codec{"test"},
      render::RenderQuality::Final,
      foundation::FilePath{"/tmp/grapple-cli-export.mov"}
    });
    if (!result) {
      printError(result.error());
      return 1;
    }

    std::cout << "revision=" << prepare.value().revision.value() << '\n';
    std::cout << "output=" << result.value().outputPath.value << '\n';
    std::cout << "frames=" << result.value().framesEvaluated << '\n';
    return 0;
  }

  if (savePackage) {
    const auto write = session.writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }

    std::cout << "snapshot=" << write.value().snapshotPath.value << '\n';
    std::cout << "manifest=" << write.value().manifestPath.value << '\n';
    return 0;
  }

  const auto viewModel = session.buildViewModel();
  if (!viewModel) {
    printError(viewModel.error());
    return 1;
  }

  std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
  std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
  std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
  std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
  std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
  std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
  std::cout << "effectGraphs=" << viewModel.value().timeline.effectGraphs.size() << '\n';
  std::cout << "diagnostics=" << renderPlan.value().diagnostics.size() << '\n';

  return 0;
}
