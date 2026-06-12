#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Time.hpp>

#include <optional>

namespace grapple::asset {

enum class AssetMediaType {
  Video,
  Audio,
  Image
};

struct AssetMetadata {
  AssetMediaType mediaType = AssetMediaType::Video;
  foundation::FilePath sourcePath;
  std::optional<foundation::FilePath> thumbnailPath;
  std::optional<foundation::TimeSeconds> duration;
  std::optional<foundation::Resolution> dimensions;
  std::optional<foundation::FrameRate> frameRate;
};

} // namespace grapple::asset

