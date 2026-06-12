#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/media/AudioBuffer.hpp>
#include <grapple/media/MediaFrame.hpp>
#include <grapple/media/MediaQuality.hpp>

namespace grapple::media {

class IMediaReader {
public:
  virtual ~IMediaReader() = default;

  virtual foundation::Result<MediaFrame> frameAt(
    foundation::AssetId assetId,
    foundation::TimeSeconds time,
    MediaQuality quality
  ) = 0;

  virtual foundation::Result<AudioBuffer> audioRange(
    foundation::AssetId assetId,
    foundation::TimeRange range,
    MediaQuality quality
  ) = 0;
};

} // namespace grapple::media

