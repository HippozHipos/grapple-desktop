#include <grapple/projection/TimelineProjector.hpp>

#include <grapple/graph/GraphNode.hpp>

namespace grapple::projection {

foundation::Result<BuildTimelineIRResult> TimelineProjector::buildTimelineIR(
  const BuildTimelineIRRequest& request
) const {
  const project::ProjectDocument& document = request.snapshot.document;

  TimelineIR timeline{
    document.info.id,
    document.revision,
    TimelineStage{document.info.name},
    foundation::TimeSeconds{0.0},
    {},
    {},
    {}
  };

  for (const graph::GraphNode& node : document.graph.nodes()) {
    if (node.kind == graph::NodeKind::Track) {
      const auto* payload = std::get_if<graph::TrackPayload>(&node.payload);
      if (payload != nullptr) {
        timeline.layers.push_back(TimelineLayer{node.id, payload->name, node.enabled});
      }
    }

    if (node.kind == graph::NodeKind::Camera) {
      const auto* payload = std::get_if<graph::CameraPayload>(&node.payload);
      if (payload != nullptr) {
        timeline.cameras.push_back(TimelineCamera{node.id, payload->name, node.enabled});
      }
    }
  }

  return BuildTimelineIRResult{timeline, timeline.diagnostics};
}

} // namespace grapple::projection

