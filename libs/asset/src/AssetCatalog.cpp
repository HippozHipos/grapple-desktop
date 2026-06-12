#include <grapple/asset/AssetCatalog.hpp>

#include <algorithm>

namespace grapple::asset {

foundation::Result<void> AssetCatalog::registerAsset(Asset asset) {
  if (!asset.id) {
    return foundation::Error{"asset.id_empty", "Asset id must not be empty."};
  }

  if (asset.name.empty()) {
    return foundation::Error{"asset.name_empty", "Asset name must not be empty."};
  }

  if (asset.metadata.sourcePath.value.empty()) {
    return foundation::Error{"asset.source_path_empty", "Asset source path must not be empty."};
  }

  const auto duplicate = std::any_of(assets_.begin(), assets_.end(), [&](const Asset& existing) {
    return existing.id == asset.id;
  });
  if (duplicate) {
    return foundation::Error{"asset.id_duplicate", "Asset id already exists."};
  }

  assets_.push_back(std::move(asset));
  return {};
}

const Asset* AssetCatalog::find(foundation::AssetId id) const noexcept {
  const auto iterator = std::find_if(assets_.begin(), assets_.end(), [&](const Asset& asset) {
    return asset.id == id;
  });

  if (iterator == assets_.end()) {
    return nullptr;
  }

  return &*iterator;
}

const std::vector<Asset>& AssetCatalog::assets() const noexcept {
  return assets_;
}

} // namespace grapple::asset

