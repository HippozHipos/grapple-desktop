#pragma once

#include <grapple/media/FrameCache.hpp>
#include <grapple/media/MediaReader.hpp>

namespace grapple::media {

class CachingMediaReader final : public IMediaReader {
public:
  CachingMediaReader(IMediaReader& source, FrameCache& frameCache);

  foundation::Result<MediaFrame> frameAt(
    foundation::AssetId assetId,
    foundation::TimeSeconds time,
    MediaQuality quality
  ) override;

  foundation::Result<AudioBuffer> audioRange(
    foundation::AssetId assetId,
    foundation::TimeRange range,
    MediaQuality quality
  ) override;

private:
  IMediaReader& source_;
  FrameCache& frameCache_;
};

} // namespace grapple::media
