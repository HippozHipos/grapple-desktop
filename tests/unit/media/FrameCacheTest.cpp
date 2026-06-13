#include <grapple/media/FrameCache.hpp>

#include <TestAssert.hpp>

#include <cstdint>
#include <vector>

int main() {
  using namespace grapple;

  media::FrameCache cache{8};
  GRAPPLE_REQUIRE(cache.maxBytes() == 8);
  GRAPPLE_REQUIRE(cache.usedBytes() == 0);
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
    "frame_1",
    {1, 2, 3, 4}
  });
  GRAPPLE_REQUIRE(put);
  GRAPPLE_REQUIRE(cache.size() == 1);
  GRAPPLE_REQUIRE(cache.usedBytes() == 4);

  const auto cached = cache.get(key);
  GRAPPLE_REQUIRE(cached.has_value());
  GRAPPLE_REQUIRE(cached->assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(cached->time == foundation::TimeSeconds{1.25});
  GRAPPLE_REQUIRE((cached->resolution == foundation::Resolution{640, 360}));
  GRAPPLE_REQUIRE(cached->frameRef == "frame_1");
  GRAPPLE_REQUIRE((cached->rgbaPixels == std::vector<std::uint8_t>{1, 2, 3, 4}));

  const auto mismatched = cache.put(key, media::MediaFrame{
    foundation::AssetId{"asset_other"},
    foundation::TimeSeconds{1.25},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "bad",
    {9, 9, 9, 9}
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

  const media::FrameCacheKey secondKey{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{2.0},
    media::MediaQuality::Proxy
  };
  const auto putSecond = cache.put(secondKey, media::MediaFrame{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{2.0},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "frame_2",
    {2, 2, 2, 2}
  });
  GRAPPLE_REQUIRE(putSecond);
  GRAPPLE_REQUIRE(cache.size() == 2);
  GRAPPLE_REQUIRE(cache.usedBytes() == 8);

  const media::FrameCacheKey thirdKey{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{3.0},
    media::MediaQuality::Proxy
  };
  const auto putThird = cache.put(thirdKey, media::MediaFrame{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{3.0},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "frame_3",
    {3, 3, 3, 3}
  });
  GRAPPLE_REQUIRE(putThird);
  GRAPPLE_REQUIRE(cache.size() == 2);
  GRAPPLE_REQUIRE(cache.usedBytes() == 8);
  GRAPPLE_REQUIRE(!cache.get(key).has_value());
  GRAPPLE_REQUIRE(cache.get(secondKey).has_value());
  GRAPPLE_REQUIRE(cache.get(thirdKey).has_value());

  const auto replaceSecond = cache.put(secondKey, media::MediaFrame{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{2.0},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "frame_2_replaced",
    {4, 4, 4, 4}
  });
  GRAPPLE_REQUIRE(replaceSecond);
  const auto replacedSecond = cache.get(secondKey);
  GRAPPLE_REQUIRE(replacedSecond.has_value());
  GRAPPLE_REQUIRE(replacedSecond->frameRef == "frame_2_replaced");
  GRAPPLE_REQUIRE((replacedSecond->rgbaPixels == std::vector<std::uint8_t>{4, 4, 4, 4}));
  GRAPPLE_REQUIRE(cache.usedBytes() == 8);

  media::FrameCache zeroCapacityCache{0};
  const auto zeroCapacityPut = zeroCapacityCache.put(key, media::MediaFrame{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{1.25},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "frame_1",
    {1, 2, 3, 4}
  });
  GRAPPLE_REQUIRE(zeroCapacityPut);
  GRAPPLE_REQUIRE(zeroCapacityCache.size() == 0);
  GRAPPLE_REQUIRE(zeroCapacityCache.usedBytes() == 0);

  media::FrameCache smallCache{2};
  const auto oversizedPut = smallCache.put(key, media::MediaFrame{
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{1.25},
    foundation::Resolution{640, 360},
    media::MediaQuality::Proxy,
    "frame_1",
    {1, 2, 3, 4}
  });
  GRAPPLE_REQUIRE(oversizedPut);
  GRAPPLE_REQUIRE(smallCache.size() == 0);
  GRAPPLE_REQUIRE(smallCache.usedBytes() == 0);

  return 0;
}
