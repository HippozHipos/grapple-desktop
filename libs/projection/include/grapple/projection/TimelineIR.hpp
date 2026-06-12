#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/projection/Diagnostics.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <string>
#include <vector>

namespace grapple::projection {

struct TimelineStage {
  std::string name;
};

struct TimelineAsset {
  foundation::AssetId assetId;
  foundation::Hash256 versionHash;
};

struct TimelineLayer {
  foundation::NodeId sourceNodeId;
  std::string name;
  bool enabled = true;
};

struct TimelineClip {
  foundation::NodeId sourceNodeId;
  foundation::NodeId trackNodeId;
  timeline::ClipPayload payload;
  bool enabled = true;
};

struct TimelineCamera {
  foundation::NodeId sourceNodeId;
  std::string name;
  timeline::Transform transform;
  timeline::CameraLens lens;
  bool enabled = true;
};

struct TimelineEffectNode {
  foundation::NodeId sourceNodeId;
  timeline::EffectPayload payload;
  bool enabled = true;
};

struct TimelineEffectEdge {
  foundation::EdgeId sourceEdgeId;
  foundation::NodeId sourceNodeId;
  graph::PortName sourcePort;
  foundation::NodeId targetNodeId;
  graph::PortName targetPort;
  std::int64_t order = 0;
  bool enabled = true;
};

struct TimelineEffectGraph {
  foundation::GraphId id;
  foundation::NodeId targetNodeId;
  std::vector<TimelineEffectNode> nodes;
  std::vector<TimelineEffectEdge> edges;
};

struct TimelineIR {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  TimelineStage stage;
  foundation::TimeSeconds duration;
  std::vector<TimelineAsset> assets;
  std::vector<TimelineLayer> layers;
  std::vector<TimelineClip> clips;
  std::vector<TimelineCamera> cameras;
  std::vector<TimelineEffectGraph> effectGraphs;
  std::vector<ProjectionDiagnostic> diagnostics;
};

} // namespace grapple::projection
