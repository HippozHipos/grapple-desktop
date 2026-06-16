#pragma once

#include <grapple/media/FrameCache.hpp>
#include <grapple/media/MediaReader.hpp>

#include <optional>

namespace grapple::media {

class CachingMediaReader final : public IMediaReader {
public:
  CachingMediaReader(IMediaReader& source, FrameCache& frameCache);

  foundation::Result<MediaFrame> frameAt(
    foundation::AssetId assetId,
    foundation::TimeSeconds time,
    std::optional<foundation::Resolution> targetResolution = std::nullopt
  ) override;

  foundation::Result<AudioBuffer> audioRange(
    foundation::AssetId assetId,
    foundation::TimeRange range
  ) override;

private:
  IMediaReader& source_;
  FrameCache& frameCache_;
};

} // namespace grapple::media
