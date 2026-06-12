#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/media/MediaQuality.hpp>

#include <vector>

namespace grapple::media {

struct AudioBuffer {
  foundation::AssetId assetId;
  foundation::TimeRange range;
  MediaQuality quality = MediaQuality::Proxy;
  int sampleRate = 48000;
  std::vector<float> samples;
};

} // namespace grapple::media

