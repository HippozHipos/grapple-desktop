#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Transform2D.hpp>

#include <optional>
#include <string>
#include <utility>

namespace grapple::timeline {

struct CompositionPayload {
  std::string name;

  friend bool operator==(const CompositionPayload&, const CompositionPayload&) = default;
};

enum class TrackKind {
  Visual,
  Audio
};

struct TrackPayload {
  TrackPayload(std::string nameValue, TrackKind kindValue)
    : name{std::move(nameValue)}, kind{kindValue} {}

  std::string name;
  TrackKind kind;

  friend bool operator==(const TrackPayload&, const TrackPayload&) = default;
};

enum class ClipKind {
  Video,
  Audio,
  Image
};

struct ClipPayload {
  ClipPayload(
    ClipKind kindValue,
    foundation::TimeRange timelineRangeValue,
    foundation::TimeRange sourceRangeValue,
    double playbackRateValue,
    foundation::AssetId assetIdValue,
    Transform2D transformValue
  )
    : kind{kindValue},
      timelineRange{timelineRangeValue},
      sourceRange{sourceRangeValue},
      playbackRate{playbackRateValue},
      assetId{std::move(assetIdValue)},
      transform{transformValue} {}

  ClipKind kind;
  foundation::TimeRange timelineRange;
  foundation::TimeRange sourceRange;
  double playbackRate;
  foundation::AssetId assetId;
  Transform2D transform;

  friend bool operator==(const ClipPayload&, const ClipPayload&) = default;
};

struct TextClipStyle {
  double fontSize = 48.0;
  foundation::Vec3 color{1.0, 1.0, 1.0};

  friend bool operator==(const TextClipStyle&, const TextClipStyle&) = default;
};

struct TextClipPayload {
  std::string text;
  foundation::TimeRange timelineRange;
  Transform2D transform;
  TextClipStyle style;

  friend bool operator==(const TextClipPayload&, const TextClipPayload&) = default;
};

struct CameraLens {
  double focalLength = 50.0;

  friend bool operator==(const CameraLens&, const CameraLens&) = default;
};

struct CameraState {
  Transform2D transform;
  CameraLens lens;

  friend bool operator==(const CameraState&, const CameraState&) = default;
};

struct CameraPayload {
  std::string name;
  CameraState state;

  friend bool operator==(const CameraPayload&, const CameraPayload&) = default;
};

struct AssetPayload {
  foundation::AssetId assetId;

  friend bool operator==(const AssetPayload&, const AssetPayload&) = default;
};

struct NotePayload {
  std::string title;
  std::string markdown;

  friend bool operator==(const NotePayload&, const NotePayload&) = default;
};

} // namespace grapple::timeline
