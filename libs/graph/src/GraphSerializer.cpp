#include <grapple/graph/GraphSerializer.hpp>

#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <algorithm>
#include <sstream>

namespace grapple::graph {

namespace {

std::string escapeString(const std::string& value) {
  std::ostringstream stream;
  for (const char character : value) {
    if (character == '\\') {
      stream << "\\\\";
    } else if (character == '"') {
      stream << "\\\"";
    } else if (character == '\n') {
      stream << "\\n";
    } else {
      stream << character;
    }
  }
  return stream.str();
}

void writeKeyValue(std::ostringstream& stream, const std::string& key, const std::string& value) {
  stream << '"' << key << "\":\"" << escapeString(value) << '"';
}

const char* nodeKindName(NodeKind kind) {
  switch (kind) {
    case NodeKind::Composition:
      return "composition";
    case NodeKind::Track:
      return "track";
    case NodeKind::Clip:
      return "clip";
    case NodeKind::Camera:
      return "camera";
    case NodeKind::Effect:
      return "effect";
    case NodeKind::Asset:
      return "asset";
    case NodeKind::Note:
      return "note";
  }
  return "unknown";
}

const char* edgeKindName(EdgeKind kind) {
  switch (kind) {
    case EdgeKind::Contains:
      return "contains";
    case EdgeKind::References:
      return "references";
    case EdgeKind::Connects:
      return "connects";
    case EdgeKind::Targets:
      return "targets";
  }
  return "unknown";
}

std::string serializePayload(const NodePayload& payload) {
  return std::visit(
    [](const auto& typedPayload) -> std::string {
      using Payload = std::decay_t<decltype(typedPayload)>;
      std::ostringstream stream;
      stream << '{';

      if constexpr (std::is_same_v<Payload, timeline::CompositionPayload>) {
        writeKeyValue(stream, "type", "composition");
        stream << ',';
        writeKeyValue(stream, "name", typedPayload.name);
      } else if constexpr (std::is_same_v<Payload, timeline::TrackPayload>) {
        writeKeyValue(stream, "type", "track");
        stream << ',';
        writeKeyValue(stream, "name", typedPayload.name);
      } else if constexpr (std::is_same_v<Payload, timeline::ClipPayload>) {
        writeKeyValue(stream, "type", "clip");
        stream << ',';
        writeKeyValue(stream, "assetId", typedPayload.assetId.value());
      } else if constexpr (std::is_same_v<Payload, timeline::CameraPayload>) {
        writeKeyValue(stream, "type", "camera");
        stream << ',';
        writeKeyValue(stream, "name", typedPayload.name);
      } else if constexpr (std::is_same_v<Payload, timeline::EffectPayload>) {
        writeKeyValue(stream, "type", "effect");
        stream << ',';
        writeKeyValue(stream, "displayName", typedPayload.displayName);
        stream << ',';
        writeKeyValue(stream, "implementationKind", std::to_string(static_cast<int>(typedPayload.implementation.kind)));
        stream << ',';
        writeKeyValue(stream, "entrypoint", typedPayload.implementation.entrypoint);
        stream << ',';
        writeKeyValue(stream, "sourceHash", typedPayload.implementation.source.sourceHash.toHex());
      } else if constexpr (std::is_same_v<Payload, timeline::AssetPayload>) {
        writeKeyValue(stream, "type", "asset");
        stream << ',';
        writeKeyValue(stream, "assetId", typedPayload.assetId.value());
      } else if constexpr (std::is_same_v<Payload, timeline::NotePayload>) {
        writeKeyValue(stream, "type", "note");
        stream << ',';
        writeKeyValue(stream, "title", typedPayload.title);
        stream << ',';
        writeKeyValue(stream, "markdown", typedPayload.markdown);
      }

      stream << '}';
      return stream.str();
    },
    payload
  );
}

} // namespace

std::string serializeCanonicalGraph(const GraphDocument& graph) {
  std::vector<GraphNode> nodes = graph.nodes();
  std::sort(nodes.begin(), nodes.end(), [](const GraphNode& left, const GraphNode& right) {
    return left.id.value() < right.id.value();
  });

  std::vector<GraphEdge> edges = graph.edges();
  std::sort(edges.begin(), edges.end(), [](const GraphEdge& left, const GraphEdge& right) {
    return left.id.value() < right.id.value();
  });

  std::ostringstream stream;
  stream << "{\"nodes\":[";
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const GraphNode& node = nodes[index];
    stream << '{';
    writeKeyValue(stream, "id", node.id.value());
    stream << ',';
    writeKeyValue(stream, "kind", nodeKindName(node.kind));
    stream << ",\"enabled\":" << (node.enabled ? "true" : "false");
    stream << ",\"payload\":" << serializePayload(node.payload);
    stream << '}';
  }

  stream << "],\"edges\":[";
  for (std::size_t index = 0; index < edges.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const GraphEdge& edge = edges[index];
    stream << '{';
    writeKeyValue(stream, "id", edge.id.value());
    stream << ',';
    writeKeyValue(stream, "kind", edgeKindName(edge.kind));
    stream << ',';
    writeKeyValue(stream, "sourceNodeId", edge.sourceNodeId.value());
    stream << ',';
    writeKeyValue(stream, "targetNodeId", edge.targetNodeId.value());
    stream << ",\"enabled\":" << (edge.enabled ? "true" : "false");
    stream << '}';
  }
  stream << "]}";

  return stream.str();
}

} // namespace grapple::graph

