#pragma once

#include <grapple/media/MediaReader.hpp>
#include <grapple/media/MediaSource.hpp>

namespace grapple::media {

class OpenCVMediaReader final : public IMediaReader {
public:
  explicit OpenCVMediaReader(const MediaSourceCatalog& sources);

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
  const MediaSourceCatalog& sources_;
};

} // namespace grapple::media
