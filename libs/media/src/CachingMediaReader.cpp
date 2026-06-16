#include <grapple/media/CachingMediaReader.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace grapple::media {

namespace {

foundation::TimeSeconds cacheTimeFor(
  const MediaSourceCatalog& sources,
  foundation::AssetId assetId,
  foundation::TimeSeconds time
) {
  const MediaSource* source = sources.find(assetId);
  if (source == nullptr ||
      source->kind != MediaSourceKind::Video ||
      !source->frameRate.has_value()) {
    return time;
  }

  const double framesPerSecond = source->frameRate->framesPerSecond();
  if (framesPerSecond <= 0.0) {
    return time;
  }

  const double frameIndex = std::floor(std::max(0.0, time.value) * framesPerSecond);
  return foundation::TimeSeconds{frameIndex / framesPerSecond};
}

} // namespace

CachingMediaReader::CachingMediaReader(
  IMediaReader& source,
  const MediaSourceCatalog& sources,
  FrameCache& frameCache
)
  : source_{source},
    sources_{sources},
    frameCache_{frameCache} {}

foundation::Result<MediaFrame> CachingMediaReader::frameAt(
  foundation::AssetId assetId,
  foundation::TimeSeconds time,
  std::optional<foundation::Resolution> targetResolution
) {
  const foundation::TimeSeconds cacheTime = cacheTimeFor(sources_, assetId, time);
  const FrameCacheKey key{assetId, cacheTime, targetResolution};
  if (auto cached = frameCache_.get(key); cached.has_value()) {
    return *cached;
  }

  auto frame = source_.frameAt(assetId, cacheTime, targetResolution);
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
  foundation::TimeRange range
) {
  return source_.audioRange(assetId, range);
}

} // namespace grapple::media
