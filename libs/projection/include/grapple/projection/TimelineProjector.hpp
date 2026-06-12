#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/projection/TimelineIR.hpp>

namespace grapple::projection {

struct BuildTimelineIRRequest {
  project::ProjectSnapshot snapshot;
};

struct BuildTimelineIRResult {
  TimelineIR timeline;
  std::vector<ProjectionDiagnostic> diagnostics;
};

class TimelineProjector {
public:
  foundation::Result<BuildTimelineIRResult> buildTimelineIR(
    const BuildTimelineIRRequest& request
  ) const;
};

} // namespace grapple::projection
