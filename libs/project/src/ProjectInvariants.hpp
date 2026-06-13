#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <cstdlib>

namespace grapple::project::invariant {

inline timeline::ClipKind clipKindForAssetMediaType(asset::AssetMediaType mediaType) {
  switch (mediaType) {
    case asset::AssetMediaType::Video:
      return timeline::ClipKind::Video;
    case asset::AssetMediaType::Audio:
      return timeline::ClipKind::Audio;
    case asset::AssetMediaType::Image:
      return timeline::ClipKind::Image;
  }

  std::abort();
}

inline foundation::Result<void> requireClipMatchesAssetMediaType(
  const timeline::ClipPayload& payload,
  const asset::Asset& asset,
  const char* code,
  const char* message
) {
  if (payload.kind != clipKindForAssetMediaType(asset.metadata.mediaType)) {
    return foundation::Error{code, message};
  }
  return {};
}

} // namespace grapple::project::invariant
