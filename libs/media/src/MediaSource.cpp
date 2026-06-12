#include <grapple/media/MediaSource.hpp>

#include <algorithm>
#include <utility>

namespace grapple::media {

foundation::Result<void> MediaSourceCatalog::registerSource(MediaSource source) {
  if (!source.assetId) {
    return foundation::Error{"media.source_asset_id_empty", "Media source asset id must not be empty."};
  }

  if (source.path.value.empty()) {
    return foundation::Error{"media.source_path_empty", "Media source path must not be empty."};
  }

  const auto existing = std::find_if(sources_.begin(), sources_.end(), [&](const MediaSource& current) {
    return current.assetId == source.assetId;
  });
  if (existing != sources_.end()) {
    return foundation::Error{"media.source_duplicate", "Media source asset id must be unique."};
  }

  sources_.push_back(std::move(source));
  return {};
}

const MediaSource* MediaSourceCatalog::find(foundation::AssetId assetId) const noexcept {
  const auto existing = std::find_if(sources_.begin(), sources_.end(), [&](const MediaSource& current) {
    return current.assetId == assetId;
  });

  return existing == sources_.end() ? nullptr : &*existing;
}

const std::vector<MediaSource>& MediaSourceCatalog::sources() const noexcept {
  return sources_;
}

} // namespace grapple::media
