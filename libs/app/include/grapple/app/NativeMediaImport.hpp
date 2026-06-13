#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Result.hpp>

namespace grapple::app {

foundation::Result<asset::Asset> inspectNativeMediaAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
);

} // namespace grapple::app
