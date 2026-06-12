#pragma once

#include <grapple/foundation/StrongId.hpp>

namespace grapple::graph {

enum class EdgeKind {
  Contains,
  References,
  Connects,
  Targets
};

struct GraphEdge {
  foundation::EdgeId id;
  EdgeKind kind = EdgeKind::Contains;
  foundation::NodeId sourceNodeId;
  foundation::NodeId targetNodeId;
  bool enabled = true;
};

} // namespace grapple::graph

