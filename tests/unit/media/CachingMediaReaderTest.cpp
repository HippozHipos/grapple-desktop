#include <grapple/media/CachingMediaReader.hpp>

#include <TestAssert.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

class TestMediaReader final : public grapple::media::IMediaReader {
public:
  grapple::foundation::Result<grapple::media::MediaFrame> frameAt(
    grapple::foundation::AssetId assetId,
    grapple::foundation::TimeSeconds time,
    grapple::media::MediaQuality quality
  ) override {
    ++frameReads;
    if (failFrames) {
      return grapple::foundation::Error{"test.frame_read_failed", "Frame read failed."};
    }

    return grapple::media::MediaFrame{
      assetId,
      time,
      grapple::foundation::Resolution{640, 360},
      quality,
      "source_frame_" + std::to_string(frameReads),
      {static_cast<std::uint8_t>(frameReads), 0, 0, 255}
    };
  }

  grapple::foundation::Result<grapple::media::AudioBuffer> audioRange(
    grapple::foundation::AssetId assetId,
    grapple::foundation::TimeRange range,
    grapple::media::MediaQuality quality
  ) override {
    ++audioReads;
    return grapple::media::AudioBuffer{
      assetId,
      range,
      quality,
      48000,
      {1.0F, -1.0F}
    };
  }

  int frameReads = 0;
  int audioReads = 0;
  bool failFrames = false;
};

} // namespace

int main() {
  using namespace grapple;

  TestMediaReader source;
  media::FrameCache frameCache{8};
  media::CachingMediaReader reader{source, frameCache};

  const auto firstFrame = reader.frameAt(
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{1.0},
    media::MediaQuality::Proxy
  );
  GRAPPLE_REQUIRE(firstFrame);
  GRAPPLE_REQUIRE(firstFrame.value().frameRef == "source_frame_1");
  GRAPPLE_REQUIRE((firstFrame.value().rgbaPixels == std::vector<std::uint8_t>{1, 0, 0, 255}));
  GRAPPLE_REQUIRE(source.frameReads == 1);
  GRAPPLE_REQUIRE(frameCache.size() == 1);

  const auto cachedFrame = reader.frameAt(
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{1.0},
    media::MediaQuality::Proxy
  );
  GRAPPLE_REQUIRE(cachedFrame);
  GRAPPLE_REQUIRE(cachedFrame.value().frameRef == "source_frame_1");
  GRAPPLE_REQUIRE((cachedFrame.value().rgbaPixels == std::vector<std::uint8_t>{1, 0, 0, 255}));
  GRAPPLE_REQUIRE(source.frameReads == 1);

  const auto secondFrame = reader.frameAt(
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{2.0},
    media::MediaQuality::Proxy
  );
  GRAPPLE_REQUIRE(secondFrame);
  GRAPPLE_REQUIRE(secondFrame.value().frameRef == "source_frame_2");
  GRAPPLE_REQUIRE(source.frameReads == 2);

  const auto audio = reader.audioRange(
    foundation::AssetId{"asset_video"},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    media::MediaQuality::Proxy
  );
  GRAPPLE_REQUIRE(audio);
  GRAPPLE_REQUIRE(source.audioReads == 1);

  source.failFrames = true;
  const auto failedRead = reader.frameAt(
    foundation::AssetId{"asset_other"},
    foundation::TimeSeconds{1.0},
    media::MediaQuality::Proxy
  );
  GRAPPLE_REQUIRE(!failedRead);
  GRAPPLE_REQUIRE(failedRead.error().code == "test.frame_read_failed");

  TestMediaReader zeroCapacitySource;
  media::FrameCache zeroCapacityCache{0};
  media::CachingMediaReader zeroCapacityReader{zeroCapacitySource, zeroCapacityCache};
  const auto uncachedFrame = zeroCapacityReader.frameAt(
    foundation::AssetId{"asset_video"},
    foundation::TimeSeconds{1.0},
    media::MediaQuality::Proxy
  );
  GRAPPLE_REQUIRE(uncachedFrame);
  GRAPPLE_REQUIRE(uncachedFrame.value().frameRef == "source_frame_1");
  GRAPPLE_REQUIRE(zeroCapacitySource.frameReads == 1);
  GRAPPLE_REQUIRE(zeroCapacityCache.size() == 0);

  return 0;
}
