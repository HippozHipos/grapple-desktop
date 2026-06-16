#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QPixmap>

#include <utility>
#include <vector>

namespace grapple::ui {

class AssetThumbnailCache final {
public:
  void setPackageRoot(foundation::FilePath packageRoot);
  void setAssets(const app::AppAssetSummary& assets);

  [[nodiscard]] const QPixmap* thumbnailFor(const foundation::AssetId& assetId) const;

private:
  [[nodiscard]] QString resolvedAssetPath(const foundation::FilePath& path) const;
  void rebuild();

  foundation::FilePath packageRoot_;
  app::AppAssetSummary assets_;
  std::vector<std::pair<foundation::AssetId, QPixmap>> thumbnails_;
};

} // namespace grapple::ui
