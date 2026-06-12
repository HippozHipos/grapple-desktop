#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <cstdint>
#include <string>

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
  foundation::EdgeId id;
  EdgeKind kind = EdgeKind::Contains;
  foundation::NodeId sourceNodeId;
  PortName sourcePort;
  foundation::NodeId targetNodeId;
  PortName targetPort;
  std::int64_t order = 0;
  bool enabled = true;
};

} // namespace grapple::graph
