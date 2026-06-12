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

struct RenderStage {
  std::string name;
};

struct RenderAsset {
  foundation::AssetId assetId;
  foundation::Hash256 versionHash;
};

struct RenderLayer {
  foundation::NodeId sourceNodeId;
  std::string name;
  bool enabled = true;
};

struct RenderClip {
  foundation::NodeId sourceNodeId;
  foundation::NodeId trackNodeId;
  timeline::ClipPayload payload;
  bool enabled = true;
};

struct RenderCamera {
  foundation::NodeId sourceNodeId;
  std::string name;
  timeline::Transform transform;
  timeline::CameraLens lens;
  bool enabled = true;
};

struct RenderEffectNode {
  foundation::NodeId sourceNodeId;
  timeline::EffectPayload payload;
  bool enabled = true;
};

struct RenderEffectEdge {
  foundation::EdgeId sourceEdgeId;
  foundation::NodeId sourceNodeId;
  graph::PortName sourcePort;
  foundation::NodeId targetNodeId;
  graph::PortName targetPort;
  std::int64_t order = 0;
  bool enabled = true;
};

struct RenderEffectGraph {
  foundation::GraphId id;
  foundation::NodeId targetNodeId;
  std::vector<RenderEffectNode> nodes;
  std::vector<RenderEffectEdge> edges;
};

struct RenderPlan {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  RenderStage stage;
  foundation::TimeSeconds duration;
  std::vector<RenderAsset> assets;
  std::vector<RenderLayer> layers;
  std::vector<RenderClip> clips;
  std::vector<RenderCamera> cameras;
  std::vector<RenderEffectGraph> effectGraphs;
  std::vector<ProjectionDiagnostic> diagnostics;
};

} // namespace grapple::projection
