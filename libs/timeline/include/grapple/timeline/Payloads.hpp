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

  friend bool operator==(const CompositionPayload&, const CompositionPayload&) = default;
};

enum class TrackKind {
  Visual,
  Audio
};

struct TrackPayload {
  std::string name;
  TrackKind kind = TrackKind::Visual;

  friend bool operator==(const TrackPayload&, const TrackPayload&) = default;
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

  friend bool operator==(const ClipPayload&, const ClipPayload&) = default;
};

struct CameraLens {
  double focalLength = 50.0;

  friend bool operator==(const CameraLens&, const CameraLens&) = default;
};

struct CameraPayload {
  std::string name;
  Transform transform;
  CameraLens lens;

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
