#pragma once

#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/media/MediaQuality.hpp>

#include <string>

namespace grapple::media {

struct MediaFrame {
  foundation::AssetId assetId;
  foundation::TimeSeconds time;
  foundation::Resolution resolution;
  MediaQuality quality = MediaQuality::Proxy;
  std::string frameRef;
};

} // namespace grapple::media

