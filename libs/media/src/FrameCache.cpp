#include <grapple/media/FrameCache.hpp>

#include <algorithm>

namespace grapple::media {

bool operator==(const FrameCacheKey& left, const FrameCacheKey& right) {
  return left.assetId == right.assetId &&
         left.time == right.time &&
         left.targetResolution == right.targetResolution;
}

namespace {

std::size_t frameBytes(const MediaFrame& frame) noexcept {
  return frame.rgbaPixels.size();
}

} // namespace

FrameCache::FrameCache(std::size_t maxBytes)
  : maxBytes_{maxBytes} {}

foundation::Result<void> FrameCache::put(FrameCacheKey key, MediaFrame frame) {
  if (!key.assetId) {
    return foundation::Error{"media.cache_asset_id_empty", "Frame cache key asset id must not be empty."};
  }

  if (frame.assetId != key.assetId || frame.time != key.time) {
    return foundation::Error{"media.cache_frame_key_mismatch", "Frame must match its cache key."};
  }

  const auto existing = std::find_if(frames_.begin(), frames_.end(), [&](const auto& entry) {
    return entry.first == key;
  });

  if (existing != frames_.end()) {
    usedBytes_ -= frameBytes(existing->second);
    frames_.erase(existing);
  }

  const std::size_t bytes = frameBytes(frame);
  if (maxBytes_ == 0 || bytes > maxBytes_) {
    return {};
  }

  while (!frames_.empty() && usedBytes_ + bytes > maxBytes_) {
    usedBytes_ -= frameBytes(frames_.front().second);
    frames_.erase(frames_.begin());
  }

  usedBytes_ += bytes;
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

std::size_t FrameCache::maxBytes() const noexcept {
  return maxBytes_;
}

std::size_t FrameCache::usedBytes() const noexcept {
  return usedBytes_;
}

} // namespace grapple::media
