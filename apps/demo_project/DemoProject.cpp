#include <DemoProject.hpp>

#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

namespace grapple::demo {

namespace {

project::CommandSource importerSource() {
  return project::CommandSource{
    project::CommandSourceKind::Importer,
    std::nullopt,
    "demo"
  };
}

project::CommandSource userSource() {
  return project::CommandSource{
    project::CommandSourceKind::User,
    std::nullopt,
    "demo"
  };
}

project::CommandSource agentSource() {
  return project::CommandSource{
    project::CommandSourceKind::Agent,
    foundation::RunId{"run_demo"},
    "demo-agent"
  };
}

} // namespace

foundation::Result<void> populateWalkingWomanDemo(
  app::NativeProjectSession& session,
  std::optional<storage::SnapshotCommitRecord> headSnapshot
) {
  app::NativeProjectCommandWriter writer{session};

  const auto registeredAsset = writer.apply(
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
    },
    importerSource()
  );
  if (!registeredAsset) {
    return registeredAsset.error();
  }

  const foundation::NodeId compositionNodeId = writer.nextNodeId("composition");
  const auto composition = writer.apply(
    project::CreateCompositionCommand{compositionNodeId, "Main"},
    userSource()
  );
  if (!composition) {
    return composition.error();
  }

  const foundation::NodeId trackNodeId = writer.nextNodeId("track");
  const auto track = writer.apply(
    project::CreateTrackCommand{
      trackNodeId,
      compositionNodeId,
      writer.nextEdgeId("contains_track"),
      "Video"
    },
    userSource()
  );
  if (!track) {
    return track.error();
  }

  const auto clip = writer.apply(
    project::CreateClipCommand{
      writer.nextNodeId("clip"),
      trackNodeId,
      writer.nextEdgeId("contains_clip"),
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
        1.0,
        foundation::AssetId{"asset_video"},
        timeline::Transform{}
      }
    },
    userSource()
  );
  if (!clip) {
    return clip.error();
  }

  const foundation::NodeId cameraNodeId = writer.nextNodeId("camera");
  const auto camera = writer.apply(
    project::CreateCameraCommand{
      cameraNodeId,
      compositionNodeId,
      writer.nextEdgeId("contains_camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::Transform{},
        timeline::CameraLens{35.0}
      }
    },
    userSource()
  );
  if (!camera) {
    return camera.error();
  }

  const std::string effectSource = "def prepare(ctx):\n  return {'camera': ctx.time}\n";
  const auto effect = writer.apply(
    project::CreateEffectCommand{
      writer.nextNodeId("effect"),
      cameraNodeId,
      writer.nextEdgeId("effect_targets_camera"),
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
    },
    agentSource(),
    std::move(headSnapshot)
  );
  if (!effect) {
    return effect.error();
  }

  return {};
}

} // namespace grapple::demo
