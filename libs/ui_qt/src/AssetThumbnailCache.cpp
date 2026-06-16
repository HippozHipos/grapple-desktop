#include <grapple/ui_qt/AssetThumbnailCache.hpp>

#include <QString>

#include <filesystem>
#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

} // namespace

void AssetThumbnailCache::setPackageRoot(foundation::FilePath packageRoot) {
  packageRoot_ = std::move(packageRoot);
  rebuild();
}

void AssetThumbnailCache::setAssets(const app::AppAssetSummary& assets) {
  assets_ = assets;
  rebuild();
}

const QPixmap* AssetThumbnailCache::thumbnailFor(const foundation::AssetId& assetId) const {
  for (const auto& thumbnail : thumbnails_) {
    if (thumbnail.first == assetId) {
      return &thumbnail.second;
    }
  }
  return nullptr;
}

QString AssetThumbnailCache::resolvedAssetPath(const foundation::FilePath& path) const {
  const std::filesystem::path filesystemPath{path.value};
  if (filesystemPath.is_absolute()) {
    return qString(filesystemPath.string());
  }
  return qString((std::filesystem::path{packageRoot_.value} / filesystemPath).lexically_normal().string());
}

void AssetThumbnailCache::rebuild() {
  thumbnails_.clear();
  thumbnails_.reserve(assets_.rows.size());
  for (const app::AppAssetRow& asset : assets_.rows) {
    if (!asset.thumbnailPath.has_value()) {
      continue;
    }
    QPixmap thumbnail{resolvedAssetPath(asset.thumbnailPath.value())};
    if (!thumbnail.isNull()) {
      thumbnails_.push_back({asset.assetId, std::move(thumbnail)});
    }
  }
}

} // namespace grapple::ui
