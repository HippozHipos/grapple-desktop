#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace grapple::graph {

enum class EdgeKind {
  Contains,
  References,
  Connects,
  Targets
};

struct PortName {
  std::string value;

  [[nodiscard]] bool empty() const noexcept {
    return value.empty();
  }

  friend bool operator==(const PortName&, const PortName&) = default;
};

struct GraphEdge {
  GraphEdge(
    foundation::EdgeId idValue,
    EdgeKind kindValue,
    foundation::NodeId sourceNodeIdValue,
    PortName sourcePortValue,
    foundation::NodeId targetNodeIdValue,
    PortName targetPortValue,
    std::int64_t orderValue,
    bool enabledValue
  )
    : id{std::move(idValue)},
      kind{kindValue},
      sourceNodeId{std::move(sourceNodeIdValue)},
      sourcePort{std::move(sourcePortValue)},
      targetNodeId{std::move(targetNodeIdValue)},
      targetPort{std::move(targetPortValue)},
      order{orderValue},
      enabled{enabledValue} {}

  foundation::EdgeId id;
  EdgeKind kind;
  foundation::NodeId sourceNodeId;
  PortName sourcePort;
  foundation::NodeId targetNodeId;
  PortName targetPort;
  std::int64_t order;
  bool enabled;
};

} // namespace grapple::graph
