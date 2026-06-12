#include <grapple/projection/RenderPlanSerializer.hpp>

#include <grapple/foundation/Json.hpp>

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <variant>

namespace grapple::projection {

namespace {

void writeNumber(std::ostringstream& stream, double value) {
  stream << std::setprecision(17) << value;
}

const char* clipKindName(timeline::ClipKind kind) {
  switch (kind) {
    case timeline::ClipKind::Video:
      return "video";
    case timeline::ClipKind::Audio:
      return "audio";
    case timeline::ClipKind::Image:
      return "image";
  }
  std::abort();
}

const char* effectImplementationKindName(timeline::EffectImplementationKind kind) {
  switch (kind) {
    case timeline::EffectImplementationKind::Builtin:
      return "builtin";
    case timeline::EffectImplementationKind::Python:
      return "python";
    case timeline::EffectImplementationKind::Shader:
      return "shader";
  }
  std::abort();
}

const char* effectSourceKindName(timeline::EffectSourceKind kind) {
  switch (kind) {
    case timeline::EffectSourceKind::InlineSource:
      return "inline_source";
    case timeline::EffectSourceKind::AssetSource:
      return "asset_source";
  }
  std::abort();
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

void writeTimeRange(std::ostringstream& stream, const foundation::TimeRange& range) {
  stream << "{\"start\":";
  writeNumber(stream, range.start.value);
  stream << ",\"end\":";
  writeNumber(stream, range.end.value);
  stream << '}';
}

void writeVec2(std::ostringstream& stream, const foundation::Vec2& value) {
  stream << "{\"x\":";
  writeNumber(stream, value.x);
  stream << ",\"y\":";
  writeNumber(stream, value.y);
  stream << '}';
}

void writeVec3(std::ostringstream& stream, const foundation::Vec3& value) {
  stream << "{\"x\":";
  writeNumber(stream, value.x);
  stream << ",\"y\":";
  writeNumber(stream, value.y);
  stream << ",\"z\":";
  writeNumber(stream, value.z);
  stream << '}';
}

void writeRect(std::ostringstream& stream, const foundation::Rect& value) {
  stream << "{\"x\":";
  writeNumber(stream, value.x);
  stream << ",\"y\":";
  writeNumber(stream, value.y);
  stream << ",\"width\":";
  writeNumber(stream, value.width);
  stream << ",\"height\":";
  writeNumber(stream, value.height);
  stream << '}';
}

void writeTransform(std::ostringstream& stream, const timeline::Transform& transform) {
  stream << "{\"position\":";
  writeVec2(stream, transform.position);
  stream << ",\"scale\":";
  writeVec2(stream, transform.scale);
  stream << ",\"rotationDegrees\":";
  writeNumber(stream, transform.rotationDegrees);
  stream << ",\"opacity\":";
  writeNumber(stream, transform.opacity);
  stream << '}';
}

void writeParamValue(std::ostringstream& stream, const timeline::ParamValue& value) {
  std::visit(
    [&](const auto& typedValue) {
      using Value = std::decay_t<decltype(typedValue)>;
      if constexpr (std::is_same_v<Value, double>) {
        writeNumber(stream, typedValue);
      } else if constexpr (std::is_same_v<Value, bool>) {
        stream << (typedValue ? "true" : "false");
      } else if constexpr (std::is_same_v<Value, std::string>) {
        stream << foundation::jsonQuoted(typedValue);
      } else if constexpr (std::is_same_v<Value, foundation::Vec2>) {
        writeVec2(stream, typedValue);
      } else if constexpr (std::is_same_v<Value, foundation::Vec3>) {
        writeVec3(stream, typedValue);
      } else if constexpr (std::is_same_v<Value, foundation::Rect>) {
        writeRect(stream, typedValue);
      }
    },
    value
  );
}

void writeClipPayload(std::ostringstream& stream, const timeline::ClipPayload& payload) {
  stream << '{';
  foundation::writeJsonStringProperty(stream, "kind", clipKindName(payload.kind));
  stream << ",\"timelineRange\":";
  writeTimeRange(stream, payload.timelineRange);
  stream << ",\"sourceRange\":";
  writeTimeRange(stream, payload.sourceRange);
  stream << ",\"playbackRate\":";
  writeNumber(stream, payload.playbackRate);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "assetId", payload.assetId.value());
  stream << ",\"transform\":";
  writeTransform(stream, payload.transform);
  stream << '}';
}

void writeEffectPayload(std::ostringstream& stream, const timeline::EffectPayload& payload) {
  stream << '{';
  foundation::writeJsonStringProperty(stream, "displayName", payload.displayName);
  stream << ",\"implementation\":{";
  foundation::writeJsonStringProperty(stream, "kind", effectImplementationKindName(payload.implementation.kind));
  stream << ',';
  foundation::writeJsonStringProperty(stream, "entrypoint", payload.implementation.entrypoint);
  stream << ",\"source\":{";
  foundation::writeJsonStringProperty(stream, "kind", effectSourceKindName(payload.implementation.source.kind));
  stream << ',';
  foundation::writeJsonStringProperty(stream, "language", payload.implementation.source.language);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "inlineSource", payload.implementation.source.inlineSource);
  stream << ",\"sourceAssetId\":";
  if (payload.implementation.source.sourceAssetId.has_value()) {
    stream << foundation::jsonQuoted(payload.implementation.source.sourceAssetId.value().value());
  } else {
    stream << "null";
  }
  stream << ',';
  foundation::writeJsonStringProperty(stream, "sourceHash", payload.implementation.source.sourceHash.toHex());
  stream << "}},\"ports\":{\"inputs\":[";
  for (std::size_t index = 0; index < payload.ports.inputs.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << '{';
    foundation::writeJsonStringProperty(stream, "name", payload.ports.inputs[index].name);
    stream << '}';
  }
  stream << "],\"outputs\":[";
  for (std::size_t index = 0; index < payload.ports.outputs.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << '{';
    foundation::writeJsonStringProperty(stream, "name", payload.ports.outputs[index].name);
    stream << '}';
  }
  stream << "]},\"params\":[";
  for (std::size_t index = 0; index < payload.params.values.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << '{';
    foundation::writeJsonStringProperty(stream, "name", payload.params.values[index].name);
    stream << ",\"value\":";
    writeParamValue(stream, payload.params.values[index].value);
    stream << '}';
  }
  stream << "],\"activeRange\":";
  writeTimeRange(stream, payload.activeRange);
  stream << '}';
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
    writeClipPayload(stream, clip.payload);
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
    writeTransform(stream, camera.transform);
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
      writeEffectPayload(stream, node.payload);
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
