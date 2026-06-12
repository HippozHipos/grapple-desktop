#include <grapple/media/FrameCache.hpp>

#include <algorithm>

namespace grapple::media {

bool operator==(const FrameCacheKey& left, const FrameCacheKey& right) {
  return left.assetId == right.assetId &&
         left.time == right.time &&
         left.quality == right.quality;
}

foundation::Result<void> FrameCache::put(FrameCacheKey key, MediaFrame frame) {
  if (!key.assetId) {
    return foundation::Error{"media.cache_asset_id_empty", "Frame cache key asset id must not be empty."};
  }

  if (frame.assetId != key.assetId || frame.time != key.time || frame.quality != key.quality) {
    return foundation::Error{"media.cache_frame_key_mismatch", "Frame must match its cache key."};
  }

  const auto existing = std::find_if(frames_.begin(), frames_.end(), [&](const auto& entry) {
    return entry.first == key;
  });

  if (existing != frames_.end()) {
    existing->second = std::move(frame);
    return {};
  }

  frames_.push_back(std::make_pair(std::move(key), std::move(frame)));
  return {};
}

std::optional<MediaFrame> FrameCache::get(const FrameCacheKey& key) const {
  const auto existing = std::find_if(frames_.begin(), frames_.end(), [&](const auto& entry) {
    return entry.first == key;
  });

  if (existing == frames_.end()) {
    return std::nullopt;
  }

  return existing->second;
}

std::size_t FrameCache::size() const noexcept {
  return frames_.size();
}

} // namespace grapple::media

