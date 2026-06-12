#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/Diagnostics.hpp>

#include <string>
#include <vector>

namespace grapple::projection {

struct TimelineStage {
  std::string name;
};

struct TimelineLayer {
  foundation::NodeId sourceNodeId;
  std::string name;
  bool enabled = true;
};

struct TimelineCamera {
  foundation::NodeId sourceNodeId;
  std::string name;
  bool enabled = true;
};

struct TimelineIR {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  TimelineStage stage;
  foundation::TimeSeconds duration;
  std::vector<TimelineLayer> layers;
  std::vector<TimelineCamera> cameras;
  std::vector<ProjectionDiagnostic> diagnostics;
};

} // namespace grapple::projection

