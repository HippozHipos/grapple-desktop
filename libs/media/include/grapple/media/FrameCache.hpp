#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/media/MediaFrame.hpp>

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
  foundation::Result<void> put(FrameCacheKey key, MediaFrame frame);
  [[nodiscard]] std::optional<MediaFrame> get(const FrameCacheKey& key) const;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::vector<std::pair<FrameCacheKey, MediaFrame>> frames_;
};

bool operator==(const FrameCacheKey& left, const FrameCacheKey& right);

} // namespace grapple::media

