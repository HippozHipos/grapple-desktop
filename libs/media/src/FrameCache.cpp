#include <grapple/media/FrameCache.hpp>

#include <algorithm>

namespace grapple::media {

bool operator==(const FrameCacheKey& left, const FrameCacheKey& right) {
  return left.assetId == right.assetId &&
         left.time == right.time &&
         left.quality == right.quality;
}

FrameCache::FrameCache(std::size_t capacity)
  : capacity_{capacity} {}

foundation::Result<void> FrameCache::put(FrameCacheKey key, MediaFrame frame) {
  if (capacity_ == 0) {
    return foundation::Error{"media.cache_capacity_empty", "Frame cache capacity must be greater than zero."};
  }

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
    const FrameCacheKey existingKey = existing->first;
    frames_.erase(existing);
    frames_.push_back(std::make_pair(existingKey, std::move(frame)));
    return {};
  }

  if (frames_.size() == capacity_) {
    frames_.erase(frames_.begin());
  }

  frames_.push_back(std::make_pair(std::move(key), std::move(frame)));
  return {};
}

std::optional<MediaFrame> FrameCache::get(const FrameCacheKey& key) {
  const auto existing = std::find_if(frames_.begin(), frames_.end(), [&](const auto& entry) {
    return entry.first == key;
  });

  if (existing == frames_.end()) {
    return std::nullopt;
  }

  auto entry = std::move(*existing);
  MediaFrame frame = entry.second;
  frames_.erase(existing);
  frames_.push_back(std::move(entry));
  return frame;
}

std::size_t FrameCache::size() const noexcept {
  return frames_.size();
}

std::size_t FrameCache::capacity() const noexcept {
  return capacity_;
}

} // namespace grapple::media
