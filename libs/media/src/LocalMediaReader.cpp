#include <grapple/media/LocalMediaReader.hpp>
#include <grapple/media/VideoDecoder.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace grapple::media {

namespace {

constexpr int ProxyMaxDimension = 720;

cv::Mat scaledForQuality(const cv::Mat& input, MediaQuality quality) {
  if (quality == MediaQuality::Full || input.empty()) {
    return input;
  }

  const int longestEdge = std::max(input.cols, input.rows);
  if (longestEdge <= ProxyMaxDimension) {
    return input;
  }

  const double scale = static_cast<double>(ProxyMaxDimension) / static_cast<double>(longestEdge);
  const cv::Size proxySize{
    std::max(1, static_cast<int>(std::lround(static_cast<double>(input.cols) * scale))),
    std::max(1, static_cast<int>(std::lround(static_cast<double>(input.rows) * scale)))
  };
  cv::Mat proxy;
  cv::resize(input, proxy, proxySize, 0.0, 0.0, cv::INTER_AREA);
  return proxy;
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
  MediaQuality quality
) {
  const cv::Mat decoded = cv::imread(source.path.value, cv::IMREAD_UNCHANGED);
  if (decoded.empty()) {
    return foundation::Error{"media.image_open_failed", "Could not decode image source " + source.path.value + "."};
  }

  const cv::Mat scaled = scaledForQuality(decoded, quality);
  auto pixels = rgbaPixelsFromMat(scaled);
  if (!pixels) {
    return pixels.error();
  }

  return MediaFrame{
    source.assetId,
    time,
    foundation::Resolution{scaled.cols, scaled.rows},
    quality,
    source.path.value,
    std::move(pixels.value())
  };
}

foundation::Result<MediaFrame> videoFrame(
  const MediaSource& source,
  foundation::TimeSeconds time,
  MediaQuality quality
) {
  auto decoded = decodeVideoFrame(source.path, time, quality);
  if (!decoded) {
    return decoded.error();
  }

  return MediaFrame{
    source.assetId,
    time,
    decoded.value().resolution,
    quality,
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
  MediaQuality quality
) {
  const MediaSource* source = impl_->sources.find(assetId);
  if (source == nullptr) {
    return foundation::Error{"media.source_missing", "Media source is not registered for asset " + assetId.value() + "."};
  }

  if (source->kind == MediaSourceKind::Image) {
    return imageFrame(*source, time, quality);
  }

  if (source->kind == MediaSourceKind::Audio) {
    return foundation::Error{"media.audio_frame_unsupported", "Audio sources do not provide video frames."};
  }

  return videoFrame(*source, time, quality);
}

foundation::Result<AudioBuffer> LocalMediaReader::audioRange(
  foundation::AssetId assetId,
  foundation::TimeRange range,
  MediaQuality quality
) {
  (void)range;
  (void)quality;

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
