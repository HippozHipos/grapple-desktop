#include <grapple/media/OpenCVMediaReader.hpp>

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
  const std::filesystem::path videoPath =
    std::filesystem::temp_directory_path() /
    ("grapple_opencv_media_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".avi");

  cv::Mat image(1, 2, CV_8UC3);
  image.at<cv::Vec3b>(0, 0) = cv::Vec3b{30, 20, 10};
  image.at<cv::Vec3b>(0, 1) = cv::Vec3b{60, 50, 40};
  GRAPPLE_REQUIRE(cv::imwrite(imagePath.string(), image));

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
  GRAPPLE_REQUIRE(sources.sources().size() == 2);
  GRAPPLE_REQUIRE(sources.find(foundation::AssetId{"asset_image"}) != nullptr);
  GRAPPLE_REQUIRE(sources.find(foundation::AssetId{"asset_video"}) != nullptr);

  const auto duplicate = sources.registerSource(media::MediaSource{
    foundation::AssetId{"asset_image"},
    media::MediaSourceKind::Image,
    foundation::FilePath{imagePath.string()}
  });
  GRAPPLE_REQUIRE(!duplicate);
  GRAPPLE_REQUIRE(duplicate.error().code == "media.source_duplicate");

  {
    media::OpenCVMediaReader reader{sources};
    const auto frame = reader.frameAt(
      foundation::AssetId{"asset_image"},
      foundation::TimeSeconds{2.0},
      media::MediaQuality::Proxy
    );
    GRAPPLE_REQUIRE(frame);
    GRAPPLE_REQUIRE(frame.value().assetId == foundation::AssetId{"asset_image"});
    GRAPPLE_REQUIRE(frame.value().time == foundation::TimeSeconds{2.0});
    GRAPPLE_REQUIRE((frame.value().resolution == foundation::Resolution{2, 1}));
    GRAPPLE_REQUIRE((frame.value().rgbaPixels == std::vector<std::uint8_t>{
      10, 20, 30, 255,
      40, 50, 60, 255
    }));

    const auto firstVideoFrame = reader.frameAt(
      foundation::AssetId{"asset_video"},
      foundation::TimeSeconds{0.0},
      media::MediaQuality::Proxy
    );
    GRAPPLE_REQUIRE(firstVideoFrame);
    GRAPPLE_REQUIRE(firstVideoFrame.value().assetId == foundation::AssetId{"asset_video"});
    GRAPPLE_REQUIRE((firstVideoFrame.value().resolution == foundation::Resolution{32, 24}));
    GRAPPLE_REQUIRE(redDominant(firstVideoFrame.value().rgbaPixels));

    const auto secondVideoFrame = reader.frameAt(
      foundation::AssetId{"asset_video"},
      foundation::TimeSeconds{0.5},
      media::MediaQuality::Proxy
    );
    GRAPPLE_REQUIRE(secondVideoFrame);
    GRAPPLE_REQUIRE(secondVideoFrame.value().assetId == foundation::AssetId{"asset_video"});
    GRAPPLE_REQUIRE((secondVideoFrame.value().resolution == foundation::Resolution{32, 24}));
    GRAPPLE_REQUIRE(greenDominant(secondVideoFrame.value().rgbaPixels));

    const auto missing = reader.frameAt(
      foundation::AssetId{"asset_missing"},
      foundation::TimeSeconds{0.0},
      media::MediaQuality::Proxy
    );
    GRAPPLE_REQUIRE(!missing);
    GRAPPLE_REQUIRE(missing.error().code == "media.source_missing");
  }

  std::filesystem::remove(imagePath);
  std::filesystem::remove(videoPath);
  return 0;
}
