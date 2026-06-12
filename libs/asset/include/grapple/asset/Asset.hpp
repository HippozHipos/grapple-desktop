#pragma once

#include <grapple/asset/AssetMetadata.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <string>

namespace grapple::asset {

struct Asset {
  foundation::AssetId id;
  std::string name;
  AssetMetadata metadata;
};

} // namespace grapple::asset

