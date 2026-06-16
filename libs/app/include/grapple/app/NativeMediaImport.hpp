#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Result.hpp>

namespace grapple::app {

foundation::Result<asset::Asset> inspectNativeMediaAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
);

foundation::Result<void> writeNativeMediaThumbnail(
  asset::AssetMediaType mediaType,
  const foundation::FilePath& mediaPath,
  const foundation::FilePath& thumbnailPath
);

} // namespace grapple::app
