#include <grapple/media/LocalMediaReader.hpp>
#include <grapple/media/VideoDecoder.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace grapple::media {

namespace {

cv::Mat scaledToTarget(
  const cv::Mat& input,
  std::optional<foundation::Resolution> targetResolution
) {
  if (!targetResolution.has_value() || input.empty()) {
    return input;
  }
  if (targetResolution->width == input.cols && targetResolution->height == input.rows) {
    return input;
  }
  const cv::Size targetSize{
    std::max(1, targetResolution->width),
    std::max(1, targetResolution->height)
  };
  cv::Mat scaled;
  const int interpolation = targetSize.width < input.cols || targetSize.height < input.rows
    ? cv::INTER_AREA
    : cv::INTER_LINEAR;
  cv::resize(input, scaled, targetSize, 0.0, 0.0, interpolation);
  return scaled;
}

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
  std::optional<foundation::Resolution> targetResolution
) {
  const cv::Mat decoded = cv::imread(source.path.value, cv::IMREAD_UNCHANGED);
  if (decoded.empty()) {
    return foundation::Error{"media.image_open_failed", "Could not decode image source " + source.path.value + "."};
  }

  const cv::Mat scaled = scaledToTarget(decoded, targetResolution);
  auto pixels = rgbaPixelsFromMat(scaled);
  if (!pixels) {
    return pixels.error();
  }

  return MediaFrame{
    source.assetId,
    time,
    foundation::Resolution{scaled.cols, scaled.rows},
    source.path.value,
    std::move(pixels.value())
  };
}

foundation::Result<MediaFrame> videoFrame(
  const MediaSource& source,
  foundation::TimeSeconds time,
  std::optional<foundation::Resolution> targetResolution
) {
  auto decoded = decodeVideoFrame(source.path, time, targetResolution);
  if (!decoded) {
    return decoded.error();
  }

  return MediaFrame{
    source.assetId,
    time,
    decoded.value().resolution,
    source.path.value,
    std::move(decoded.value().rgbaPixels)
  };
}

} // namespace

struct LocalMediaReader::Impl {
  explicit Impl(const MediaSourceCatalog& sourceCatalog)
    : sources{sourceCatalog} {}

  const MediaSourceCatalog& sources;
};

LocalMediaReader::LocalMediaReader(const MediaSourceCatalog& sources)
  : impl_{std::make_unique<Impl>(sources)} {}

LocalMediaReader::~LocalMediaReader() = default;

LocalMediaReader::LocalMediaReader(LocalMediaReader&&) noexcept = default;

LocalMediaReader& LocalMediaReader::operator=(LocalMediaReader&&) noexcept = default;

foundation::Result<MediaFrame> LocalMediaReader::frameAt(
  foundation::AssetId assetId,
  foundation::TimeSeconds time,
  std::optional<foundation::Resolution> targetResolution
) {
  const MediaSource* source = impl_->sources.find(assetId);
  if (source == nullptr) {
    return foundation::Error{"media.source_missing", "Media source is not registered for asset " + assetId.value() + "."};
  }

  if (source->kind == MediaSourceKind::Image) {
    return imageFrame(*source, time, targetResolution);
  }

  if (source->kind == MediaSourceKind::Audio) {
    return foundation::Error{"media.audio_frame_unsupported", "Audio sources do not provide video frames."};
  }

  return videoFrame(*source, time, targetResolution);
}

foundation::Result<AudioBuffer> LocalMediaReader::audioRange(
  foundation::AssetId assetId,
  foundation::TimeRange range
) {
  (void)range;

  const MediaSource* source = impl_->sources.find(assetId);
  if (source == nullptr) {
    return foundation::Error{"media.source_missing", "Media source is not registered for asset " + assetId.value() + "."};
  }

  if (source->kind != MediaSourceKind::Audio) {
    return foundation::Error{"media.audio_source_kind_invalid", "Audio ranges require an audio media source."};
  }

  return foundation::Error{"media.audio_decode_unsupported", "LocalMediaReader does not decode audio for asset " + assetId.value() + "."};
}

} // namespace grapple::media
