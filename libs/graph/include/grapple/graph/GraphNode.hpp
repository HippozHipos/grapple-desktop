#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <utility>
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
  GraphNode(
    foundation::NodeId idValue,
    NodeKind kindValue,
    NodePayload payloadValue,
    bool enabledValue
  )
    : id{std::move(idValue)},
      kind{kindValue},
      payload{std::move(payloadValue)},
      enabled{enabledValue} {}

  foundation::NodeId id;
  NodeKind kind;
  NodePayload payload;
  bool enabled;
};

} // namespace grapple::graph
