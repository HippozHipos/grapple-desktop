#include <grapple/media/OpenCVMediaReader.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

namespace grapple::media {

namespace {

foundation::Result<std::vector<std::uint8_t>> rgbaPixelsFromMat(const cv::Mat& input) {
  if (input.empty()) {
    return foundation::Error{"media.frame_empty", "Decoded media frame must not be empty."};
  }

  cv::Mat rgba;
  if (input.channels() == 4) {
    cv::cvtColor(input, rgba, cv::COLOR_BGRA2RGBA);
  } else if (input.channels() == 3) {
    cv::cvtColor(input, rgba, cv::COLOR_BGR2RGBA);
  } else if (input.channels() == 1) {
    cv::cvtColor(input, rgba, cv::COLOR_GRAY2RGBA);
  } else {
    return foundation::Error{"media.frame_channels_unsupported", "Decoded media frame channel count is unsupported."};
  }

  if (!rgba.isContinuous()) {
    rgba = rgba.clone();
  }

  const auto byteCount = static_cast<std::size_t>(rgba.total() * rgba.elemSize());
  return std::vector<std::uint8_t>{rgba.data, rgba.data + byteCount};
}

foundation::Result<MediaFrame> imageFrame(
  const MediaSource& source,
  foundation::TimeSeconds time,
  MediaQuality quality
) {
  const cv::Mat decoded = cv::imread(source.path.value, cv::IMREAD_UNCHANGED);
  if (decoded.empty()) {
    return foundation::Error{"media.image_open_failed", "Could not decode image source " + source.path.value + "."};
  }

  auto pixels = rgbaPixelsFromMat(decoded);
  if (!pixels) {
    return pixels.error();
  }

  return MediaFrame{
    source.assetId,
    time,
    foundation::Resolution{decoded.cols, decoded.rows},
    quality,
    source.path.value,
    std::move(pixels.value())
  };
}

foundation::Result<MediaFrame> videoFrame(
  const MediaSource& source,
  foundation::TimeSeconds time,
  MediaQuality quality,
  cv::VideoCapture& capture
) {
  if (!capture.isOpened()) {
    return foundation::Error{"media.video_open_failed", "Could not open video source " + source.path.value + "."};
  }

  capture.set(cv::CAP_PROP_POS_MSEC, time.value * 1000.0);

  cv::Mat decoded;
  if (!capture.read(decoded) || decoded.empty()) {
    return foundation::Error{"media.video_frame_decode_failed", "Could not decode requested video frame."};
  }

  auto pixels = rgbaPixelsFromMat(decoded);
  if (!pixels) {
    return pixels.error();
  }

  return MediaFrame{
    source.assetId,
    time,
    foundation::Resolution{decoded.cols, decoded.rows},
    quality,
    source.path.value,
    std::move(pixels.value())
  };
}

} // namespace

struct OpenCVMediaReader::Impl {
  explicit Impl(const MediaSourceCatalog& sourceCatalog)
    : sources{sourceCatalog} {}

  foundation::Result<cv::VideoCapture*> videoCaptureFor(const MediaSource& source) {
    auto [iterator, inserted] = videoCaptures.try_emplace(source.assetId.value());
    cv::VideoCapture& capture = iterator->second;
    if (inserted || !capture.isOpened()) {
      capture.open(source.path.value);
    }
    if (!capture.isOpened()) {
      return foundation::Error{"media.video_open_failed", "Could not open video source " + source.path.value + "."};
    }
    return &capture;
  }

  const MediaSourceCatalog& sources;
  std::unordered_map<std::string, cv::VideoCapture> videoCaptures;
};

OpenCVMediaReader::OpenCVMediaReader(const MediaSourceCatalog& sources)
  : impl_{std::make_unique<Impl>(sources)} {}

OpenCVMediaReader::~OpenCVMediaReader() = default;

OpenCVMediaReader::OpenCVMediaReader(OpenCVMediaReader&&) noexcept = default;

OpenCVMediaReader& OpenCVMediaReader::operator=(OpenCVMediaReader&&) noexcept = default;

foundation::Result<MediaFrame> OpenCVMediaReader::frameAt(
  foundation::AssetId assetId,
  foundation::TimeSeconds time,
  MediaQuality quality
) {
  const MediaSource* source = impl_->sources.find(assetId);
  if (source == nullptr) {
    return foundation::Error{"media.source_missing", "Media source is not registered for asset " + assetId.value() + "."};
  }

  if (source->kind == MediaSourceKind::Image) {
    return imageFrame(*source, time, quality);
  }

  auto capture = impl_->videoCaptureFor(*source);
  if (!capture) {
    return capture.error();
  }
  return videoFrame(*source, time, quality, *capture.value());
}

foundation::Result<AudioBuffer> OpenCVMediaReader::audioRange(
  foundation::AssetId assetId,
  foundation::TimeRange range,
  MediaQuality quality
) {
  return foundation::Error{"media.audio_decode_unsupported", "OpenCVMediaReader does not decode audio for asset " + assetId.value() + "."};
}

} // namespace grapple::media
