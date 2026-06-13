#pragma once

#include <grapple/asset/AssetMetadata.hpp>
#include <grapple/media/MediaSource.hpp>

#include <cstdlib>

namespace grapple::app {

inline media::MediaSourceKind mediaSourceKindForAssetMediaType(asset::AssetMediaType mediaType) {
  switch (mediaType) {
    case asset::AssetMediaType::Video:
      return media::MediaSourceKind::Video;
    case asset::AssetMediaType::Audio:
      return media::MediaSourceKind::Audio;
    case asset::AssetMediaType::Image:
      return media::MediaSourceKind::Image;
  }

  std::abort();
}

} // namespace grapple::app
