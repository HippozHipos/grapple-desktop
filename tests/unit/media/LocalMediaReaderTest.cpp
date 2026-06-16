#include <grapple/media/LocalMediaReader.hpp>
#include <grapple/media/VideoDecoder.hpp>

#include <TestAssert.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

bool redDominant(const std::vector<std::uint8_t>& rgbaPixels) {
  return rgbaPixels.size() >= 4 &&
         rgbaPixels[0] > 120 &&
         rgbaPixels[0] > rgbaPixels[1] &&
         rgbaPixels[0] > rgbaPixels[2];
}

bool greenDominant(const std::vector<std::uint8_t>& rgbaPixels) {
  return rgbaPixels.size() >= 4 &&
         rgbaPixels[1] > 120 &&
         rgbaPixels[1] > rgbaPixels[0] &&
         rgbaPixels[1] > rgbaPixels[2];
}

} // namespace

int main() {
  using namespace grapple;

  const std::filesystem::path imagePath =
    std::filesystem::temp_directory_path() /
    ("grapple_opencv_media_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
  const std::filesystem::path largeImagePath =
    std::filesystem::temp_directory_path() /
    ("grapple_opencv_media_large_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
  const std::filesystem::path videoPath =
    std::filesystem::temp_directory_path() /
    ("grapple_opencv_media_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".avi");
  const std::filesystem::path largeVideoPath =
    std::filesystem::temp_directory_path() /
    ("grapple_local_media_large_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".avi");

  cv::Mat image(1, 2, CV_8UC3);
  image.at<cv::Vec3b>(0, 0) = cv::Vec3b{30, 20, 10};
  image.at<cv::Vec3b>(0, 1) = cv::Vec3b{60, 50, 40};
  GRAPPLE_REQUIRE(cv::imwrite(imagePath.string(), image));
  cv::Mat largeImage(800, 1200, CV_8UC3);
  largeImage.setTo(cv::Scalar{20, 120, 220});
  GRAPPLE_REQUIRE(cv::imwrite(largeImagePath.string(), largeImage));

  cv::VideoWriter writer{
    videoPath.string(),
    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
    2.0,
    cv::Size{32, 24}
  };
  GRAPPLE_REQUIRE(writer.isOpened());
  cv::Mat redFrame(24, 32, CV_8UC3);
  redFrame.setTo(cv::Scalar{0, 0, 240});
  cv::Mat greenFrame(24, 32, CV_8UC3);
  greenFrame.setTo(cv::Scalar{0, 240, 0});
  writer.write(redFrame);
  writer.write(greenFrame);
  writer.release();
  cv::VideoWriter largeWriter{
    largeVideoPath.string(),
    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
    1.0,
    cv::Size{1200, 800}
  };
  GRAPPLE_REQUIRE(largeWriter.isOpened());
  cv::Mat largeVideoFrame(800, 1200, CV_8UC3);
  largeVideoFrame.setTo(cv::Scalar{20, 80, 220});
  largeWriter.write(largeVideoFrame);
  largeWriter.release();

  auto decodeSession = media::VideoDecodeSession::open(foundation::FilePath{videoPath.string()});
  GRAPPLE_REQUIRE(decodeSession);
  const auto sessionMetadata = decodeSession.value().metadata();
  GRAPPLE_REQUIRE(sessionMetadata);
  GRAPPLE_REQUIRE((sessionMetadata.value().resolution == foundation::Resolution{32, 24}));
  const auto sessionFirstFrame = decodeSession.value().frameAt(foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(sessionFirstFrame);
  GRAPPLE_REQUIRE(redDominant(sessionFirstFrame.value().rgbaPixels));
  const auto sessionSecondFrame = decodeSession.value().frameAt(foundation::TimeSeconds{0.5});
  GRAPPLE_REQUIRE(sessionSecondFrame);
  GRAPPLE_REQUIRE(greenDominant(sessionSecondFrame.value().rgbaPixels));

  media::MediaSourceCatalog sources;
  const auto registerSource = sources.registerSource(media::MediaSource{
    foundation::AssetId{"asset_image"},
    media::MediaSourceKind::Image,
    foundation::FilePath{imagePath.string()}
  });
  GRAPPLE_REQUIRE(registerSource);
  const auto registerVideoSource = sources.registerSource(media::MediaSource{
    foundation::AssetId{"asset_video"},
    media::MediaSourceKind::Video,
    foundation::FilePath{videoPath.string()}
  });
  GRAPPLE_REQUIRE(registerVideoSource);
  const auto registerLargeImageSource = sources.registerSource(media::MediaSource{
    foundation::AssetId{"asset_large_image"},
    media::MediaSourceKind::Image,
    foundation::FilePath{largeImagePath.string()}
  });
  GRAPPLE_REQUIRE(registerLargeImageSource);
  const auto registerLargeVideoSource = sources.registerSource(media::MediaSource{
    foundation::AssetId{"asset_large_video"},
    media::MediaSourceKind::Video,
    foundation::FilePath{largeVideoPath.string()}
  });
  GRAPPLE_REQUIRE(registerLargeVideoSource);
  const auto registerAudioSource = sources.registerSource(media::MediaSource{
    foundation::AssetId{"asset_audio"},
    media::MediaSourceKind::Audio,
    foundation::FilePath{"/tmp/grapple-audio.wav"}
  });
  GRAPPLE_REQUIRE(registerAudioSource);
  GRAPPLE_REQUIRE(sources.sources().size() == 5);
  GRAPPLE_REQUIRE(sources.find(foundation::AssetId{"asset_image"}) != nullptr);
  GRAPPLE_REQUIRE(sources.find(foundation::AssetId{"asset_video"}) != nullptr);
  GRAPPLE_REQUIRE(sources.find(foundation::AssetId{"asset_audio"}) != nullptr);

  const auto duplicate = sources.registerSource(media::MediaSource{
    foundation::AssetId{"asset_image"},
    media::MediaSourceKind::Image,
    foundation::FilePath{imagePath.string()}
  });
  GRAPPLE_REQUIRE(!duplicate);
  GRAPPLE_REQUIRE(duplicate.error().code == "media.source_duplicate");

  {
    media::LocalMediaReader reader{sources};
    const auto frame = reader.frameAt(
      foundation::AssetId{"asset_image"},
      foundation::TimeSeconds{2.0}
    );
    GRAPPLE_REQUIRE(frame);
    GRAPPLE_REQUIRE(frame.value().assetId == foundation::AssetId{"asset_image"});
    GRAPPLE_REQUIRE(frame.value().time == foundation::TimeSeconds{2.0});
    GRAPPLE_REQUIRE((frame.value().resolution == foundation::Resolution{2, 1}));
    GRAPPLE_REQUIRE((frame.value().rgbaPixels == std::vector<std::uint8_t>{
      10, 20, 30, 255,
      40, 50, 60, 255
    }));

    const auto largeFrame = reader.frameAt(
      foundation::AssetId{"asset_large_image"},
      foundation::TimeSeconds{0.0}
    );
    GRAPPLE_REQUIRE(largeFrame);
    GRAPPLE_REQUIRE((largeFrame.value().resolution == foundation::Resolution{1200, 800}));
    const auto largeTargetedFrame = reader.frameAt(
      foundation::AssetId{"asset_large_image"},
      foundation::TimeSeconds{0.0},
      foundation::Resolution{720, 480}
    );
    GRAPPLE_REQUIRE(largeTargetedFrame);
    GRAPPLE_REQUIRE((largeTargetedFrame.value().resolution == foundation::Resolution{720, 480}));

    const auto firstVideoFrame = reader.frameAt(
      foundation::AssetId{"asset_video"},
      foundation::TimeSeconds{0.0}
    );
    GRAPPLE_REQUIRE(firstVideoFrame);
    GRAPPLE_REQUIRE(firstVideoFrame.value().assetId == foundation::AssetId{"asset_video"});
    GRAPPLE_REQUIRE((firstVideoFrame.value().resolution == foundation::Resolution{32, 24}));
    GRAPPLE_REQUIRE(redDominant(firstVideoFrame.value().rgbaPixels));

    const auto secondVideoFrame = reader.frameAt(
      foundation::AssetId{"asset_video"},
      foundation::TimeSeconds{0.5}
    );
    GRAPPLE_REQUIRE(secondVideoFrame);
    GRAPPLE_REQUIRE(secondVideoFrame.value().assetId == foundation::AssetId{"asset_video"});
    GRAPPLE_REQUIRE((secondVideoFrame.value().resolution == foundation::Resolution{32, 24}));
    GRAPPLE_REQUIRE(greenDominant(secondVideoFrame.value().rgbaPixels));

    const auto largeVideoFrame = reader.frameAt(
      foundation::AssetId{"asset_large_video"},
      foundation::TimeSeconds{0.0}
    );
    GRAPPLE_REQUIRE(largeVideoFrame);
    GRAPPLE_REQUIRE((largeVideoFrame.value().resolution == foundation::Resolution{1200, 800}));
    const auto largeVideoTargetedFrame = reader.frameAt(
      foundation::AssetId{"asset_large_video"},
      foundation::TimeSeconds{0.0},
      foundation::Resolution{720, 480}
    );
    GRAPPLE_REQUIRE(largeVideoTargetedFrame);
    GRAPPLE_REQUIRE((largeVideoTargetedFrame.value().resolution == foundation::Resolution{720, 480}));

    const auto missing = reader.frameAt(
      foundation::AssetId{"asset_missing"},
      foundation::TimeSeconds{0.0}
    );
    GRAPPLE_REQUIRE(!missing);
    GRAPPLE_REQUIRE(missing.error().code == "media.source_missing");

    const auto audioFrame = reader.frameAt(
      foundation::AssetId{"asset_audio"},
      foundation::TimeSeconds{0.0}
    );
    GRAPPLE_REQUIRE(!audioFrame);
    GRAPPLE_REQUIRE(audioFrame.error().code == "media.audio_frame_unsupported");

    const auto missingAudio = reader.audioRange(
      foundation::AssetId{"asset_missing"},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
    );
    GRAPPLE_REQUIRE(!missingAudio);
    GRAPPLE_REQUIRE(missingAudio.error().code == "media.source_missing");

    const auto imageAudio = reader.audioRange(
      foundation::AssetId{"asset_image"},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
    );
    GRAPPLE_REQUIRE(!imageAudio);
    GRAPPLE_REQUIRE(imageAudio.error().code == "media.audio_source_kind_invalid");

    const auto audio = reader.audioRange(
      foundation::AssetId{"asset_audio"},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
    );
    GRAPPLE_REQUIRE(!audio);
    GRAPPLE_REQUIRE(audio.error().code == "media.audio_decode_unsupported");
  }

  std::filesystem::remove(imagePath);
  std::filesystem::remove(largeImagePath);
  std::filesystem::remove(videoPath);
  std::filesystem::remove(largeVideoPath);
  return 0;
}
