#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <string>
#include <variant>

namespace grapple::graph {

enum class NodeKind {
  Composition,
  Track,
  Clip,
  Camera,
  Effect,
  Asset,
  Note
};

struct CompositionPayload {
  std::string name;
};

struct TrackPayload {
  std::string name;
};

struct ClipPayload {
  foundation::AssetId assetId;
};

struct CameraPayload {
  std::string name;
};

struct EffectPayload {
  std::string implementationId;
};

struct AssetPayload {
  foundation::AssetId assetId;
};

struct NotePayload {
  std::string text;
};

using NodePayload = std::variant<
  CompositionPayload,
  TrackPayload,
  ClipPayload,
  CameraPayload,
  EffectPayload,
  AssetPayload,
  NotePayload
>;

struct GraphNode {
  foundation::NodeId id;
  NodeKind kind = NodeKind::Composition;
  NodePayload payload = CompositionPayload{};
  bool enabled = true;
};

} // namespace grapple::graph

