#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/media/MediaQuality.hpp>

#include <cstdint>
#include <vector>

namespace grapple::media {

struct VideoMetadata {
  foundation::TimeSeconds duration;
  foundation::Resolution resolution;
  foundation::FrameRate frameRate;
};

struct DecodedVideoFrame {
  foundation::Resolution resolution;
  std::vector<std::uint8_t> rgbaPixels;
};

foundation::Result<VideoMetadata> inspectVideoFile(const foundation::FilePath& path);

foundation::Result<DecodedVideoFrame> decodeVideoFrame(
  const foundation::FilePath& path,
  foundation::TimeSeconds time,
  MediaQuality quality
);

} // namespace grapple::media
