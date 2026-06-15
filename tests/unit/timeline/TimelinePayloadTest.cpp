#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/ParamSampling.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/timeline/TimelineSerializer.hpp>

#include <TestAssert.hpp>

#include <string>

int main() {
  using namespace grapple;

  const timeline::TrackPayload track{"Video", timeline::TrackKind::Visual};
  GRAPPLE_REQUIRE(track.name == "Video");
  GRAPPLE_REQUIRE(track.kind == timeline::TrackKind::Visual);

  const timeline::ClipPayload clip{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{6.0}},
    1.0,
    foundation::AssetId{"asset_video"},
    timeline::Transform2D{}
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
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}},
    {
      timeline::EffectModelDependency{
        foundation::ModelId{"model_segmenter"},
        foundation::stableHash("segmenter_v1")
      }
    }
  };
  GRAPPLE_REQUIRE(effect.implementation.kind == timeline::EffectImplementationKind::Python);
  GRAPPLE_REQUIRE(effect.implementation.source.language == "python");
  GRAPPLE_REQUIRE(effect.implementation.source.sourceHash == foundation::stableHash("def process(): pass"));
  GRAPPLE_REQUIRE(timeline::serializeCanonicalCameraPayload(timeline::CameraPayload{
    "Camera",
    timeline::CameraState{
      timeline::Transform2D{},
      timeline::CameraLens{35.0}
    }
  }).find("\"focalLength\":35") != std::string::npos);
  GRAPPLE_REQUIRE(timeline::serializeCanonicalClipPayload(clip).find("\"assetId\":\"asset_video\"") != std::string::npos);
  GRAPPLE_REQUIRE(timeline::serializeCanonicalEffectPayload(effect).find("\"inlineSource\":\"def process(): pass\"") != std::string::npos);
  GRAPPLE_REQUIRE(timeline::serializeCanonicalEffectPayload(effect).find("\"modelDependencies\":[{\"modelId\":\"model_segmenter\",\"versionHash\":\"") != std::string::npos);
  GRAPPLE_REQUIRE(timeline::serializeCanonicalParamSet(timeline::ParamSet{
    {timeline::Param{"target_x", 0.5}, timeline::Param{"enabled", true}}
  }) == "[{\"name\":\"enabled\",\"value\":true,\"keyframes\":[]},{\"name\":\"target_x\",\"value\":0.5,\"keyframes\":[]}]");
  GRAPPLE_REQUIRE(timeline::serializeCanonicalParamSet(timeline::ParamSet{
    {timeline::Param{
      "smoothing",
      0.25,
      timeline::Param::Control{
        "Smoothing",
        timeline::Param::NumericControl{0.0, 1.0, 0.01}
      },
      {
        timeline::Param::Keyframe{foundation::KeyframeId{"key_smooth_2"}, foundation::TimeSeconds{2.0}, 0.75},
        timeline::Param::Keyframe{foundation::KeyframeId{"key_smooth_1"}, foundation::TimeSeconds{1.0}, 0.5}
      }
    }}
  }) == "[{\"name\":\"smoothing\",\"label\":\"Smoothing\",\"numeric\":{\"min\":0,\"max\":1,\"step\":0.01},\"value\":0.25,\"keyframes\":[{\"id\":\"key_smooth_1\",\"time\":1,\"value\":0.5},{\"id\":\"key_smooth_2\",\"time\":2,\"value\":0.75}]}]");

  const timeline::Param animatedNumber{
    "position_x",
    0.25,
    timeline::Param::Control{},
    {
      timeline::Param::Keyframe{foundation::KeyframeId{"key_position_x_2"}, foundation::TimeSeconds{2.0}, 1.0},
      timeline::Param::Keyframe{foundation::KeyframeId{"key_position_x_0"}, foundation::TimeSeconds{0.0}, 0.0}
    }
  };
  GRAPPLE_REQUIRE(std::get<double>(timeline::sampleParamValue(animatedNumber, foundation::TimeSeconds{-1.0})) == 0.0);
  GRAPPLE_REQUIRE(std::get<double>(timeline::sampleParamValue(animatedNumber, foundation::TimeSeconds{1.0})) == 0.5);
  GRAPPLE_REQUIRE(std::get<double>(timeline::sampleParamValue(animatedNumber, foundation::TimeSeconds{2.0})) == 1.0);
  GRAPPLE_REQUIRE(std::get<double>(timeline::sampleParamValue(animatedNumber, foundation::TimeSeconds{3.0})) == 1.0);

  const timeline::Param animatedToggle{
    "enabled",
    false,
    timeline::Param::Control{},
    {
      timeline::Param::Keyframe{foundation::KeyframeId{"key_enabled_0"}, foundation::TimeSeconds{0.0}, false},
      timeline::Param::Keyframe{foundation::KeyframeId{"key_enabled_2"}, foundation::TimeSeconds{2.0}, true}
    }
  };
  GRAPPLE_REQUIRE(!std::get<bool>(timeline::sampleParamValue(animatedToggle, foundation::TimeSeconds{1.0})));
  GRAPPLE_REQUIRE(std::get<bool>(timeline::sampleParamValue(animatedToggle, foundation::TimeSeconds{2.0})));

  return 0;
}
