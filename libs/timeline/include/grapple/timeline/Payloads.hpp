#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Transform.hpp>

#include <optional>
#include <string>

namespace grapple::timeline {

struct CompositionPayload {
  std::string name;
};

struct TrackPayload {
  std::string name;
};

enum class ClipKind {
  Video,
  Audio,
  Image
};

struct ClipPayload {
  ClipKind kind = ClipKind::Video;
  foundation::TimeRange timelineRange;
  foundation::TimeRange sourceRange;
  double playbackRate = 1.0;
  foundation::AssetId assetId;
  Transform transform;
};

struct CameraLens {
  double focalLength = 50.0;
};

struct CameraPayload {
  std::string name;
  Transform transform;
  CameraLens lens;
};

struct AssetPayload {
  foundation::AssetId assetId;
};

struct NotePayload {
  std::string title;
  std::string markdown;
};

} // namespace grapple::timeline

