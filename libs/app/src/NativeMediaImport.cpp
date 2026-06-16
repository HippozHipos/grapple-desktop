#include <grapple/app/NativeMediaImport.hpp>
#include <grapple/media/VideoDecoder.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

namespace grapple::app {

namespace {

constexpr int ThumbnailMaxEdge = 320;

std::optional<std::uint32_t> readLittleEndianU32(std::istream& input) {
  unsigned char bytes[4]{};
  input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!input) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::optional<std::uint16_t> readLittleEndianU16(std::istream& input) {
  unsigned char bytes[2]{};
  input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!input) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::optional<foundation::TimeSeconds> readWavDuration(const foundation::FilePath& path) {
  std::ifstream input{path.value, std::ios::binary};
  if (!input) {
    return std::nullopt;
  }

  char riff[4]{};
  char wave[4]{};
  input.read(riff, sizeof(riff));
  const auto riffSize = readLittleEndianU32(input);
  input.read(wave, sizeof(wave));
  if (!riffSize.has_value() || std::string_view{riff, 4} != "RIFF" || std::string_view{wave, 4} != "WAVE") {
    return std::nullopt;
  }

  std::optional<std::uint16_t> channels;
  std::optional<std::uint32_t> sampleRate;
  std::optional<std::uint16_t> bitsPerSample;
  std::optional<std::uint32_t> dataBytes;

  while (input) {
    char chunkId[4]{};
    input.read(chunkId, sizeof(chunkId));
    if (!input) {
      break;
    }
    const auto chunkSize = readLittleEndianU32(input);
    if (!chunkSize.has_value()) {
      return std::nullopt;
    }

    const std::string_view chunk{chunkId, 4};
    if (chunk == "fmt " && chunkSize.value() >= 16U) {
      const auto audioFormat = readLittleEndianU16(input);
      channels = readLittleEndianU16(input);
      sampleRate = readLittleEndianU32(input);
      input.seekg(6, std::ios::cur);
      bitsPerSample = readLittleEndianU16(input);
      if (!audioFormat.has_value() || audioFormat.value() != 1U || !channels.has_value() || !sampleRate.has_value() || !bitsPerSample.has_value()) {
        return std::nullopt;
      }
      const std::uint32_t remaining = chunkSize.value() - 16U;
      if (remaining > 0U) {
        input.seekg(static_cast<std::streamoff>(remaining), std::ios::cur);
      }
    } else if (chunk == "data") {
      dataBytes = chunkSize.value();
      input.seekg(static_cast<std::streamoff>(chunkSize.value()), std::ios::cur);
    } else {
      input.seekg(static_cast<std::streamoff>(chunkSize.value()), std::ios::cur);
    }

    if ((chunkSize.value() % 2U) == 1U) {
      input.seekg(1, std::ios::cur);
    }
  }

  if (!channels.has_value() || !sampleRate.has_value() || !bitsPerSample.has_value() || !dataBytes.has_value()) {
    return std::nullopt;
  }
  const std::uint32_t bytesPerSampleFrame = static_cast<std::uint32_t>(channels.value()) *
                                            (static_cast<std::uint32_t>(bitsPerSample.value()) / 8U);
  if (bytesPerSampleFrame == 0U || sampleRate.value() == 0U) {
    return std::nullopt;
  }
  return foundation::TimeSeconds{
    static_cast<double>(dataBytes.value()) /
      static_cast<double>(bytesPerSampleFrame * sampleRate.value())
  };
}

std::string lowerExtension(const foundation::FilePath& path) {
  std::string extension = std::filesystem::path{path.value}.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return extension;
}

foundation::Result<asset::AssetMediaType> mediaTypeForPath(const foundation::FilePath& path) {
  const std::string extension = lowerExtension(path);
  if (extension == ".mov" || extension == ".mp4" || extension == ".avi" || extension == ".mkv") {
    return asset::AssetMediaType::Video;
  }
  if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".ppm" || extension == ".webp") {
    return asset::AssetMediaType::Image;
  }
  if (extension == ".wav" || extension == ".aiff" || extension == ".aif" || extension == ".mp3" || extension == ".flac") {
    return asset::AssetMediaType::Audio;
  }
  return foundation::Error{"app.media_type_unsupported", "Unsupported media file extension for " + path.value + "."};
}

foundation::Resolution fitWithinLargestEdge(foundation::Resolution source, int largestEdge) {
  const int sourceLargestEdge = std::max(source.width, source.height);
  if (sourceLargestEdge <= 0 || sourceLargestEdge <= largestEdge) {
    return source;
  }
  const double scale = static_cast<double>(largestEdge) / static_cast<double>(sourceLargestEdge);
  return foundation::Resolution{
    std::max(1, static_cast<int>(std::lround(static_cast<double>(source.width) * scale))),
    std::max(1, static_cast<int>(std::lround(static_cast<double>(source.height) * scale)))
  };
}

foundation::Result<cv::Mat> decodeThumbnailSource(
  asset::AssetMediaType mediaType,
  const foundation::FilePath& mediaPath
) {
  switch (mediaType) {
    case asset::AssetMediaType::Image: {
      cv::Mat decoded = cv::imread(mediaPath.value, cv::IMREAD_COLOR);
      if (decoded.empty()) {
        return foundation::Error{"app.thumbnail_image_open_failed", "Could not decode image thumbnail source " + mediaPath.value + "."};
      }
      return decoded;
    }
    case asset::AssetMediaType::Video: {
      auto metadata = media::inspectVideoFile(mediaPath);
      if (!metadata) {
        return metadata.error();
      }
      auto decoded = media::decodeVideoFrame(
        mediaPath,
        foundation::TimeSeconds{0.0},
        fitWithinLargestEdge(metadata.value().resolution, ThumbnailMaxEdge)
      );
      if (!decoded) {
        return decoded.error();
      }
      cv::Mat rgba{
        decoded.value().resolution.height,
        decoded.value().resolution.width,
        CV_8UC4,
        decoded.value().rgbaPixels.data()
      };
      cv::Mat bgr;
      cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
      return bgr;
    }
    case asset::AssetMediaType::Audio:
      return foundation::Error{"app.thumbnail_media_type_invalid", "Thumbnails require image or video media."};
  }

  std::abort();
}

cv::Mat resizedThumbnail(const cv::Mat& source) {
  const int largestEdge = std::max(source.cols, source.rows);
  if (largestEdge <= ThumbnailMaxEdge) {
    return source;
  }

  const double scale = static_cast<double>(ThumbnailMaxEdge) / static_cast<double>(largestEdge);
  cv::Mat resized;
  cv::resize(source, resized, cv::Size{}, scale, scale, cv::INTER_AREA);
  return resized;
}

foundation::Result<asset::Asset> inspectVideoAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  auto metadata = media::inspectVideoFile(path);
  if (!metadata) {
    return metadata.error();
  }

  const std::filesystem::path filesystemPath{path.value};
  return asset::Asset{
    assetId,
    filesystemPath.stem().string(),
    asset::AssetMetadata{
      asset::AssetMediaType::Video,
      path,
      std::nullopt,
      metadata.value().duration,
      metadata.value().resolution,
      metadata.value().frameRate
    }
  };
}

foundation::Result<asset::Asset> inspectImageAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  const cv::Mat decoded = cv::imread(path.value, cv::IMREAD_UNCHANGED);
  if (decoded.empty()) {
    return foundation::Error{"app.image_open_failed", "Could not inspect image file " + path.value + "."};
  }

  const std::filesystem::path filesystemPath{path.value};
  return asset::Asset{
    assetId,
    filesystemPath.stem().string(),
    asset::AssetMetadata{
      asset::AssetMediaType::Image,
      path,
      std::nullopt,
      std::nullopt,
      foundation::Resolution{decoded.cols, decoded.rows},
      std::nullopt
    }
  };
}

foundation::Result<asset::Asset> inspectAudioAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  if (!std::filesystem::is_regular_file(std::filesystem::path{path.value})) {
    return foundation::Error{"app.audio_file_missing", "Audio file does not exist: " + path.value + "."};
  }

  const std::filesystem::path filesystemPath{path.value};
  return asset::Asset{
    assetId,
    filesystemPath.stem().string(),
    asset::AssetMetadata{
      asset::AssetMediaType::Audio,
      path,
      std::nullopt,
      readWavDuration(path),
      std::nullopt,
      std::nullopt
    }
  };
}

} // namespace

foundation::Result<asset::Asset> inspectNativeMediaAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  auto mediaType = mediaTypeForPath(path);
  if (!mediaType) {
    return mediaType.error();
  }

  switch (mediaType.value()) {
    case asset::AssetMediaType::Video:
      return inspectVideoAsset(assetId, path);
    case asset::AssetMediaType::Audio:
      return inspectAudioAsset(assetId, path);
    case asset::AssetMediaType::Image:
      return inspectImageAsset(assetId, path);
  }

  std::abort();
}

foundation::Result<void> writeNativeMediaThumbnail(
  asset::AssetMediaType mediaType,
  const foundation::FilePath& mediaPath,
  const foundation::FilePath& thumbnailPath
) {
  auto decoded = decodeThumbnailSource(mediaType, mediaPath);
  if (!decoded) {
    return decoded.error();
  }

  const cv::Mat thumbnail = resizedThumbnail(decoded.value());
  if (thumbnail.empty()) {
    return foundation::Error{"app.thumbnail_empty", "Decoded thumbnail image must not be empty."};
  }
  if (!cv::imwrite(thumbnailPath.value, thumbnail)) {
    return foundation::Error{"app.thumbnail_write_failed", "Could not write media thumbnail " + thumbnailPath.value + "."};
  }
  return {};
}

} // namespace grapple::app
