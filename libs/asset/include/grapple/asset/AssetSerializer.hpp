#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/asset/AssetCatalog.hpp>

#include <string>

namespace grapple::asset {

std::string serializeCanonicalAsset(const Asset& asset);
std::string serializeCanonicalAssetCatalog(const AssetCatalog& catalog);

} // namespace grapple::asset
