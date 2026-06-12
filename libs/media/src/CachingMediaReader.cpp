#include <grapple/media/CachingMediaReader.hpp>

#include <utility>

namespace grapple::media {

CachingMediaReader::CachingMediaReader(IMediaReader& source, FrameCache& frameCache)
  : source_{source},
    frameCache_{frameCache} {}

foundation::Result<MediaFrame> CachingMediaReader::frameAt(
  foundation::AssetId assetId,
  foundation::TimeSeconds time,
  MediaQuality quality
) {
  const FrameCacheKey key{assetId, time, quality};
  if (auto cached = frameCache_.get(key); cached.has_value()) {
    return *cached;
  }

  auto frame = source_.frameAt(assetId, time, quality);
  if (!frame) {
    return frame.error();
  }

  auto cachePut = frameCache_.put(key, frame.value());
  if (!cachePut) {
    return cachePut.error();
  }

  return std::move(frame.value());
}

foundation::Result<AudioBuffer> CachingMediaReader::audioRange(
  foundation::AssetId assetId,
  foundation::TimeRange range,
  MediaQuality quality
) {
  return source_.audioRange(assetId, range, quality);
}

} // namespace grapple::media
