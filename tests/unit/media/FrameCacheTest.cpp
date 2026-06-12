#include <grapple/media/FrameCache.hpp>

#include <TestAssert.hpp>

int main() {
  using namespace grapple;

  media::FrameCache cache;
  const media::FrameCacheKey key{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{1.25},
    media::MediaQuality::Proxy
  };

  const auto put = cache.put(key, media::MediaFrame{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{1.25},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "frame_1"
  });
  GRAPPLE_REQUIRE(put);
  GRAPPLE_REQUIRE(cache.size() == 1);

  const auto cached = cache.get(key);
  GRAPPLE_REQUIRE(cached.has_value());
  GRAPPLE_REQUIRE(cached->assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(cached->time == foundation::TimeSeconds{1.25});
  GRAPPLE_REQUIRE((cached->resolution == foundation::Resolution{640, 360}));
  GRAPPLE_REQUIRE(cached->frameRef == "frame_1");

  const auto mismatched = cache.put(key, media::MediaFrame{
    foundation::AssetId{"asset_other"},
    foundation::TimeSeconds{1.25},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "bad"
  });
  GRAPPLE_REQUIRE(!mismatched);
  GRAPPLE_REQUIRE(mismatched.error().code == "media.cache_frame_key_mismatch");
  GRAPPLE_REQUIRE(cache.size() == 1);

  const auto missing = cache.get(media::FrameCacheKey{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{2.0},
    media::MediaQuality::Proxy
  });
  GRAPPLE_REQUIRE(!missing.has_value());

  return 0;
}

