#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <optional>
#include <string>

namespace grapple::runtime {

enum class RuntimeSourceKind {
  InlineSource,
  AssetSource
};

struct RuntimeSource {
  RuntimeSourceKind kind = RuntimeSourceKind::InlineSource;
  std::string language;
  std::string inlineSource;
  std::optional<foundation::AssetId> sourceAssetId;
  foundation::Hash256 sourceHash;

  friend bool operator==(const RuntimeSource&, const RuntimeSource&) = default;
};

} // namespace grapple::runtime
