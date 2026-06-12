#include <grapple/projection/RenderPlanSerializer.hpp>

#include <grapple/foundation/Json.hpp>
#include <grapple/timeline/TimelineSerializer.hpp>

#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace grapple::projection {

namespace {

void writeNumber(std::ostringstream& stream, double value) {
  stream << std::setprecision(17) << value;
}

const char* diagnosticSeverityName(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Info:
      return "info";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Error:
      return "error";
  }
  std::abort();
}

} // namespace

std::string serializeCanonicalRenderPlan(const RenderPlan& plan) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "projectId", plan.projectId.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "revision", plan.revision.value());
  stream << ",\"stage\":{";
  foundation::writeJsonStringProperty(stream, "name", plan.stage.name);
  stream << "},\"duration\":";
  writeNumber(stream, plan.duration.value);
  stream << ",\"layers\":[";
  for (std::size_t index = 0; index < plan.layers.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderLayer& layer = plan.layers[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", layer.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "name", layer.name);
    stream << ",\"enabled\":" << (layer.enabled ? "true" : "false") << '}';
  }
  stream << "],\"clips\":[";
  for (std::size_t index = 0; index < plan.clips.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderClip& clip = plan.clips[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", clip.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "trackNodeId", clip.trackNodeId.value());
    stream << ",\"payload\":";
    stream << timeline::serializeCanonicalClipPayload(clip.payload);
    stream << ",\"enabled\":" << (clip.enabled ? "true" : "false") << '}';
  }
  stream << "],\"cameras\":[";
  for (std::size_t index = 0; index < plan.cameras.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderCamera& camera = plan.cameras[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", camera.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "name", camera.name);
    stream << ",\"transform\":";
    stream << timeline::serializeCanonicalTransform(camera.transform);
    stream << ",\"lens\":{\"focalLength\":";
    writeNumber(stream, camera.lens.focalLength);
    stream << "},\"enabled\":" << (camera.enabled ? "true" : "false") << '}';
  }
  stream << "],\"effectGraphs\":[";
  for (std::size_t graphIndex = 0; graphIndex < plan.effectGraphs.size(); ++graphIndex) {
    if (graphIndex != 0) {
      stream << ',';
    }
    const RenderEffectGraph& effectGraph = plan.effectGraphs[graphIndex];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "id", effectGraph.id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "targetNodeId", effectGraph.targetNodeId.value());
    stream << ",\"nodes\":[";
    for (std::size_t nodeIndex = 0; nodeIndex < effectGraph.nodes.size(); ++nodeIndex) {
      if (nodeIndex != 0) {
        stream << ',';
      }
      const RenderEffectNode& node = effectGraph.nodes[nodeIndex];
      stream << '{';
      foundation::writeJsonStringProperty(stream, "sourceNodeId", node.sourceNodeId.value());
      stream << ",\"payload\":";
      stream << timeline::serializeCanonicalEffectPayload(node.payload);
      stream << ",\"enabled\":" << (node.enabled ? "true" : "false") << '}';
    }
    stream << "],\"edges\":[";
    for (std::size_t edgeIndex = 0; edgeIndex < effectGraph.edges.size(); ++edgeIndex) {
      if (edgeIndex != 0) {
        stream << ',';
      }
      const RenderEffectEdge& edge = effectGraph.edges[edgeIndex];
      stream << '{';
      foundation::writeJsonStringProperty(stream, "sourceEdgeId", edge.sourceEdgeId.value());
      stream << ',';
      foundation::writeJsonStringProperty(stream, "sourceNodeId", edge.sourceNodeId.value());
      stream << ',';
      foundation::writeJsonStringProperty(stream, "targetNodeId", edge.targetNodeId.value());
      stream << ",\"enabled\":" << (edge.enabled ? "true" : "false") << '}';
    }
    stream << "]}";
  }
  stream << "],\"diagnostics\":[";
  for (std::size_t index = 0; index < plan.diagnostics.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const ProjectionDiagnostic& diagnostic = plan.diagnostics[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "code", diagnostic.code);
    stream << ',';
    foundation::writeJsonStringProperty(stream, "severity", diagnosticSeverityName(diagnostic.severity));
    stream << ",\"location\":{";
    foundation::writeJsonStringProperty(stream, "projectId", diagnostic.location.projectId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "revision", diagnostic.location.revision.value());
    stream << ",\"nodeId\":";
    if (diagnostic.location.nodeId.has_value()) {
      stream << foundation::jsonQuoted(diagnostic.location.nodeId.value().value());
    } else {
      stream << "null";
    }
    stream << "},";
    foundation::writeJsonStringProperty(stream, "message", diagnostic.message);
    stream << '}';
  }
  stream << "]}";
  return stream.str();
}

} // namespace grapple::projection
