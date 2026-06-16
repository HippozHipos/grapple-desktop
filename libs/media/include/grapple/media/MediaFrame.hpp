#pragma once

#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace grapple::media {

struct MediaFrame {
  foundation::AssetId assetId;
  foundation::TimeSeconds time;
  foundation::Resolution resolution;
  std::string frameRef;
  std::vector<std::uint8_t> rgbaPixels;
};

} // namespace grapple::media
