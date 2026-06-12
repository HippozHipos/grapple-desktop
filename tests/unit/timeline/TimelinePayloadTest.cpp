#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/timeline/TimelineSerializer.hpp>

#include <TestAssert.hpp>

#include <string>

int main() {
  using namespace grapple;

  const timeline::TrackPayload track{"Video"};
  GRAPPLE_REQUIRE(track.name == "Video");

  const timeline::ClipPayload clip{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{6.0}},
    1.0,
    foundation::AssetId{"asset_video"},
    timeline::Transform{}
  };
  GRAPPLE_REQUIRE(clip.timelineRange.duration() == 5.0);
  GRAPPLE_REQUIRE(clip.sourceRange.start == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(clip.assetId == foundation::AssetId{"asset_video"});

  const timeline::EffectPayload effect{
    "Subject Follow",
    timeline::EffectImplementation{
      timeline::EffectImplementationKind::Python,
      "process",
      timeline::EffectSource{
        timeline::EffectSourceKind::InlineSource,
        "python",
        "def process(): pass",
        std::nullopt,
        foundation::stableHash("def process(): pass")
      }
    },
    timeline::EffectPortSet{},
    timeline::ParamSet{},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}}
  };
  GRAPPLE_REQUIRE(effect.implementation.kind == timeline::EffectImplementationKind::Python);
  GRAPPLE_REQUIRE(effect.implementation.source.language == "python");
  GRAPPLE_REQUIRE(effect.implementation.source.sourceHash == foundation::stableHash("def process(): pass"));
  GRAPPLE_REQUIRE(timeline::serializeCanonicalCameraPayload(timeline::CameraPayload{"Camera", timeline::Transform{}, timeline::CameraLens{35.0}}).find("\"focalLength\":35") != std::string::npos);
  GRAPPLE_REQUIRE(timeline::serializeCanonicalClipPayload(clip).find("\"assetId\":\"asset_video\"") != std::string::npos);
  GRAPPLE_REQUIRE(timeline::serializeCanonicalEffectPayload(effect).find("\"inlineSource\":\"def process(): pass\"") != std::string::npos);

  return 0;
}
