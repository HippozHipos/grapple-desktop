#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/Diagnostics.hpp>

#include <string>
#include <vector>

namespace grapple::projection {

struct RenderStage {
  std::string name;
};

struct RenderLayer {
  foundation::NodeId sourceNodeId;
  std::string name;
  bool enabled = true;
};

struct RenderCamera {
  foundation::NodeId sourceNodeId;
  std::string name;
  bool enabled = true;
};

struct RenderEffectGraph {
  foundation::GraphId id;
  foundation::NodeId targetNodeId;
};

struct RenderPlan {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  RenderStage stage;
  foundation::TimeSeconds duration;
  std::vector<RenderLayer> layers;
  std::vector<RenderCamera> cameras;
  std::vector<RenderEffectGraph> effectGraphs;
  std::vector<ProjectionDiagnostic> diagnostics;
};

} // namespace grapple::projection

