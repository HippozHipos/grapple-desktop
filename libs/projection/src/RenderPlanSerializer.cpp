#include <grapple/projection/RenderPlanSerializer.hpp>

#include <grapple/foundation/Json.hpp>
#include <grapple/timeline/TimelineSerializer.hpp>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <tuple>

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

int diagnosticSeverityRank(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Info:
      return 0;
    case DiagnosticSeverity::Warning:
      return 1;
    case DiagnosticSeverity::Error:
      return 2;
  }
  std::abort();
}

std::string optionalNodeIdValue(const std::optional<foundation::NodeId>& nodeId) {
  return nodeId.has_value() ? nodeId->value() : "";
}

} // namespace

std::string serializeCanonicalRenderPlan(const RenderPlan& plan) {
  std::vector<RenderLayer> layers = plan.layers;
  std::sort(layers.begin(), layers.end(), [](const RenderLayer& left, const RenderLayer& right) {
    return left.sourceNodeId < right.sourceNodeId;
  });

  std::vector<RenderClip> clips = plan.clips;
  std::sort(clips.begin(), clips.end(), [](const RenderClip& left, const RenderClip& right) {
    return left.sourceNodeId < right.sourceNodeId;
  });

  std::vector<RenderAudioClip> audioClips = plan.audioClips;
  std::sort(audioClips.begin(), audioClips.end(), [](const RenderAudioClip& left, const RenderAudioClip& right) {
    return left.sourceNodeId < right.sourceNodeId;
  });

  std::vector<RenderAsset> assets = plan.assets;
  std::sort(assets.begin(), assets.end(), [](const RenderAsset& left, const RenderAsset& right) {
    return left.assetId < right.assetId;
  });

  std::vector<RenderCamera> cameras = plan.cameras;
  std::sort(cameras.begin(), cameras.end(), [](const RenderCamera& left, const RenderCamera& right) {
    return left.sourceNodeId < right.sourceNodeId;
  });

  std::vector<RenderEffectGraph> effectGraphs = plan.effectGraphs;
  std::sort(effectGraphs.begin(), effectGraphs.end(), [](const RenderEffectGraph& left, const RenderEffectGraph& right) {
    return left.id < right.id;
  });
  for (RenderEffectGraph& effectGraph : effectGraphs) {
    std::sort(effectGraph.nodes.begin(), effectGraph.nodes.end(), [](const RenderEffectNode& left, const RenderEffectNode& right) {
      return left.sourceNodeId < right.sourceNodeId;
    });
    std::sort(effectGraph.edges.begin(), effectGraph.edges.end(), [](const RenderEffectEdge& left, const RenderEffectEdge& right) {
      return left.sourceEdgeId < right.sourceEdgeId;
    });
  }

  std::vector<ProjectionDiagnostic> diagnostics = plan.diagnostics;
  std::sort(diagnostics.begin(), diagnostics.end(), [](const ProjectionDiagnostic& left, const ProjectionDiagnostic& right) {
    return std::make_tuple(
      left.code,
      diagnosticSeverityRank(left.severity),
      left.location.projectId,
      left.location.revision,
      optionalNodeIdValue(left.location.nodeId),
      left.message
    ) < std::make_tuple(
      right.code,
      diagnosticSeverityRank(right.severity),
      right.location.projectId,
      right.location.revision,
      optionalNodeIdValue(right.location.nodeId),
      right.message
    );
  });

  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "projectId", plan.projectId.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "revision", plan.revision.value());
  stream << ",\"stage\":{";
  foundation::writeJsonStringProperty(stream, "name", plan.stage.name);
  stream << "},\"duration\":";
  writeNumber(stream, plan.duration.value);
  stream << ",\"assets\":[";
  for (std::size_t index = 0; index < assets.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderAsset& asset = assets[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "assetId", asset.assetId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "versionHash", asset.versionHash.toHex());
    stream << '}';
  }
  stream << "],\"layers\":[";
  for (std::size_t index = 0; index < layers.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderLayer& layer = layers[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", layer.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "name", layer.name);
    stream << '}';
  }
  stream << "],\"clips\":[";
  for (std::size_t index = 0; index < clips.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderClip& clip = clips[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", clip.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "trackNodeId", clip.trackNodeId.value());
    stream << ",\"payload\":";
    stream << timeline::serializeCanonicalClipPayload(clip.payload);
    stream << '}';
  }
  stream << "],\"audioClips\":[";
  for (std::size_t index = 0; index < audioClips.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderAudioClip& clip = audioClips[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", clip.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "trackNodeId", clip.trackNodeId.value());
    stream << ",\"payload\":";
    stream << timeline::serializeCanonicalClipPayload(clip.payload);
    stream << '}';
  }
  stream << "],\"cameras\":[";
  for (std::size_t index = 0; index < cameras.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const RenderCamera& camera = cameras[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", camera.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "name", camera.name);
    stream << ",\"transform\":";
    stream << timeline::serializeCanonicalTransform(camera.transform);
    stream << ",\"lens\":{\"focalLength\":";
    writeNumber(stream, camera.lens.focalLength);
    stream << "}}";
  }
  stream << "],\"effectGraphs\":[";
  for (std::size_t graphIndex = 0; graphIndex < effectGraphs.size(); ++graphIndex) {
    if (graphIndex != 0) {
      stream << ',';
    }
    const RenderEffectGraph& effectGraph = effectGraphs[graphIndex];
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
      stream << '}';
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
      foundation::writeJsonStringProperty(stream, "sourcePort", edge.sourcePort.value);
      stream << ',';
      foundation::writeJsonStringProperty(stream, "targetNodeId", edge.targetNodeId.value());
      stream << ',';
      foundation::writeJsonStringProperty(stream, "targetPort", edge.targetPort.value);
      stream << ",\"order\":" << edge.order;
      stream << '}';
    }
    stream << "]}";
  }
  stream << "],\"diagnostics\":[";
  for (std::size_t index = 0; index < diagnostics.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const ProjectionDiagnostic& diagnostic = diagnostics[index];
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
