#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/timeline/Payloads.hpp>

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

using NodePayload = std::variant<
  timeline::CompositionPayload,
  timeline::TrackPayload,
  timeline::ClipPayload,
  timeline::CameraPayload,
  timeline::EffectPayload,
  timeline::AssetPayload,
  timeline::NotePayload
>;

struct GraphNode {
  foundation::NodeId id;
  NodeKind kind = NodeKind::Composition;
  NodePayload payload = timeline::CompositionPayload{};
  bool enabled = true;
};

} // namespace grapple::graph
