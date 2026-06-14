#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Time.hpp>

#include <optional>
#include <utility>

namespace grapple::asset {

enum class AssetMediaType {
  Video,
  Audio,
  Image
};

struct AssetMetadata {
  AssetMetadata(
    AssetMediaType mediaTypeValue,
    foundation::FilePath sourcePathValue,
    std::optional<foundation::FilePath> thumbnailPathValue,
    std::optional<foundation::TimeSeconds> durationValue,
    std::optional<foundation::Resolution> dimensionsValue,
    std::optional<foundation::FrameRate> frameRateValue
  )
    : mediaType{mediaTypeValue},
      sourcePath{std::move(sourcePathValue)},
      thumbnailPath{std::move(thumbnailPathValue)},
      duration{durationValue},
      dimensions{dimensionsValue},
      frameRate{frameRateValue} {}

  AssetMediaType mediaType;
  foundation::FilePath sourcePath;
  std::optional<foundation::FilePath> thumbnailPath;
  std::optional<foundation::TimeSeconds> duration;
  std::optional<foundation::Resolution> dimensions;
  std::optional<foundation::FrameRate> frameRate;
};

} // namespace grapple::asset
