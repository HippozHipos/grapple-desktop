#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/media/MediaFrame.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace grapple::media {

struct FrameCacheKey {
  foundation::AssetId assetId;
  foundation::TimeSeconds time;
  MediaQuality quality = MediaQuality::Proxy;
};

class FrameCache {
public:
  explicit FrameCache(std::size_t maxBytes);

  foundation::Result<void> put(FrameCacheKey key, MediaFrame frame);
  [[nodiscard]] std::optional<MediaFrame> get(const FrameCacheKey& key);
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t maxBytes() const noexcept;
  [[nodiscard]] std::size_t usedBytes() const noexcept;

private:
  std::size_t maxBytes_;
  std::size_t usedBytes_ = 0;
  std::vector<std::pair<FrameCacheKey, MediaFrame>> frames_;
};

bool operator==(const FrameCacheKey& left, const FrameCacheKey& right);

} // namespace grapple::media
