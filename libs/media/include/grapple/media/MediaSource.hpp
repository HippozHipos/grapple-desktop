#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <vector>

namespace grapple::media {

enum class MediaSourceKind {
  Video,
  Audio,
  Image
};

struct MediaSource {
  foundation::AssetId assetId;
  MediaSourceKind kind = MediaSourceKind::Video;
  foundation::FilePath path;
};

class MediaSourceCatalog {
public:
  foundation::Result<void> registerSource(MediaSource source);
  [[nodiscard]] const MediaSource* find(foundation::AssetId assetId) const noexcept;
  [[nodiscard]] const std::vector<MediaSource>& sources() const noexcept;

private:
  std::vector<MediaSource> sources_;
};

} // namespace grapple::media
