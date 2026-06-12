#include <DemoProject.hpp>

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <chrono>
#include <utility>

namespace grapple::demo {

namespace {

storage::ProjectCommitRecordOptions commandOptions() {
  return storage::ProjectCommitRecordOptions{
    std::chrono::system_clock::now(),
    std::nullopt
  };
}

storage::ProjectCommitRecordOptions commandOptions(storage::SnapshotCommitRecord snapshot) {
  return storage::ProjectCommitRecordOptions{
    std::chrono::system_clock::now(),
    std::move(snapshot)
  };
}

} // namespace

foundation::Result<void> populateWalkingWomanDemo(
  app::NativeProjectSession& session,
  std::optional<storage::SnapshotCommitRecord> headSnapshot
) {
  const auto initialSnapshot = session.snapshot();
  if (!initialSnapshot) {
    return initialSnapshot.error();
  }
  const foundation::ProjectId projectId = initialSnapshot.value().info.id;

  const auto registeredAsset = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_register_asset"},
      projectId,
      initialSnapshot.value().revision,
      project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "demo"},
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
    return registeredAsset.error();
  }

  const auto composition = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_composition"},
      projectId,
      registeredAsset.value().commandResult.afterRevision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "demo"},
      project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
    },
    commandOptions()
  );
  if (!composition) {
    return composition.error();
  }

  const auto track = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_track"},
      projectId,
      composition.value().commandResult.afterRevision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "demo"},
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
    return track.error();
  }

  const auto clip = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_clip"},
      projectId,
      track.value().commandResult.afterRevision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "demo"},
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
    return clip.error();
  }

  const auto camera = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_camera"},
      projectId,
      clip.value().commandResult.afterRevision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "demo"},
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
    return camera.error();
  }

  const std::string effectSource = "def prepare(ctx):\n  return {'camera': ctx.time}\n";
  const auto effect = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_create_effect"},
      projectId,
      camera.value().commandResult.afterRevision,
      project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_demo"}, "demo-agent"},
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
    headSnapshot.has_value()
      ? commandOptions(std::move(*headSnapshot))
      : commandOptions()
  );
  if (!effect) {
    return effect.error();
  }

  return {};
}

} // namespace grapple::demo
