#pragma once

#include <grapple/asset/AssetMetadata.hpp>

#include <string>

namespace grapple::asset {

struct AssetImportRequest {
  std::string name;
  AssetMetadata metadata;
};

} // namespace grapple::asset

