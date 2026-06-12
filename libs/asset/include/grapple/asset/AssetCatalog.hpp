#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Result.hpp>

#include <vector>

namespace grapple::asset {

class AssetCatalog {
public:
  foundation::Result<void> registerAsset(Asset asset);

  [[nodiscard]] const Asset* find(foundation::AssetId id) const noexcept;
  [[nodiscard]] const std::vector<Asset>& assets() const noexcept;

private:
  std::vector<Asset> assets_;
};

} // namespace grapple::asset

