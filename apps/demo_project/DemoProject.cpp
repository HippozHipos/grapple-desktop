#include <DemoProject.hpp>

#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <filesystem>
#include <system_error>

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

} // namespace

foundation::Result<void> ensureStarterDemoVideo() {
  constexpr int width = 320;
  constexpr int height = 180;
  constexpr int frameCount = 300;
  const foundation::FilePath path{"/tmp/grapple-native-demo/starter-gradient.avi"};
  const std::filesystem::path videoPath{path.value};
  std::filesystem::create_directories(videoPath.parent_path());
  if (std::filesystem::exists(videoPath) && std::filesystem::file_size(videoPath) > 0U) {
    return {};
  }

  const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path temporaryPath = videoPath.parent_path() /
                                             (videoPath.filename().string() + "." + std::to_string(uniqueSuffix) + ".tmp");

  cv::VideoWriter writer{
    temporaryPath.string(),
    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
    30.0,
    cv::Size{width, height}
  };
  if (!writer.isOpened()) {
    return foundation::Error{"demo.video_open_failed", "Could not create demo video " + path.value + "."};
  }

  for (int frame = 0; frame < frameCount; ++frame) {
    cv::Mat image(height, width, CV_8UC3);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        image.at<cv::Vec3b>(y, x) = cv::Vec3b{
          static_cast<unsigned char>((180 + frame * 2) % 255),
          static_cast<unsigned char>((y * 2 + 80) % 255),
          static_cast<unsigned char>((x + frame * 4) % 255)
        };
      }
    }
    writer.write(image);
  }
  writer.release();

  std::error_code renameError;
  std::filesystem::rename(temporaryPath, videoPath, renameError);
  if (renameError) {
    if (std::filesystem::exists(videoPath) && std::filesystem::file_size(videoPath) > 0U) {
      std::filesystem::remove(temporaryPath);
      return {};
    }
    std::filesystem::remove(temporaryPath);
    return foundation::Error{"demo.video_rename_failed", "Could not install demo video " + path.value + "."};
  }

  return {};
}

foundation::Result<void> populateStarterDemo(
  app::NativeProjectSession& session,
  std::optional<storage::SnapshotCommitRecord> headSnapshot
) {
  app::NativeProjectCommandWriter writer{session};

  const auto registeredAsset = writer.apply(
    project::RegisterAssetCommand{
      asset::Asset{
        foundation::AssetId{"asset_video"},
        "Starter Gradient",
        asset::AssetMetadata{
          asset::AssetMediaType::Video,
          foundation::FilePath{"/tmp/grapple-native-demo/starter-gradient.avi"},
          std::nullopt,
          foundation::TimeSeconds{10.0},
          foundation::Resolution{320, 180},
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
      "Video",
      timeline::TrackKind::Visual
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
        timeline::Transform2D{}
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
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource(),
    std::move(headSnapshot)
  );
  if (!camera) {
    return camera.error();
  }

  return {};
}

} // namespace grapple::demo
