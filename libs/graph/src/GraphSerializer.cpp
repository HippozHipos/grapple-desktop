#include <grapple/graph/GraphSerializer.hpp>

#include <grapple/foundation/Json.hpp>
#include <grapple/timeline/EffectPayload.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/timeline/TimelineSerializer.hpp>

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace grapple::graph {

namespace {

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
  std::abort();
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
  std::abort();
}

std::string serializePayload(const NodePayload& payload) {
  return std::visit(
    [](const auto& typedPayload) -> std::string {
      using Payload = std::decay_t<decltype(typedPayload)>;
      std::ostringstream stream;
      stream << '{';

      if constexpr (std::is_same_v<Payload, timeline::CompositionPayload>) {
        foundation::writeJsonStringProperty(stream, "type", "composition");
        stream << ',';
        foundation::writeJsonStringProperty(stream, "name", typedPayload.name);
      } else if constexpr (std::is_same_v<Payload, timeline::TrackPayload>) {
        foundation::writeJsonStringProperty(stream, "type", "track");
        stream << ",\"payload\":" << timeline::serializeCanonicalTrackPayload(typedPayload);
      } else if constexpr (std::is_same_v<Payload, timeline::ClipPayload>) {
        foundation::writeJsonStringProperty(stream, "type", "clip");
        stream << ",\"payload\":" << timeline::serializeCanonicalClipPayload(typedPayload);
      } else if constexpr (std::is_same_v<Payload, timeline::TextClipPayload>) {
        foundation::writeJsonStringProperty(stream, "type", "text_clip");
        stream << ",\"payload\":" << timeline::serializeCanonicalTextClipPayload(typedPayload);
      } else if constexpr (std::is_same_v<Payload, timeline::CameraPayload>) {
        foundation::writeJsonStringProperty(stream, "type", "camera");
        stream << ",\"payload\":" << timeline::serializeCanonicalCameraPayload(typedPayload);
      } else if constexpr (std::is_same_v<Payload, timeline::EffectPayload>) {
        foundation::writeJsonStringProperty(stream, "type", "effect");
        stream << ",\"payload\":" << timeline::serializeCanonicalEffectPayload(typedPayload);
      } else if constexpr (std::is_same_v<Payload, timeline::AssetPayload>) {
        foundation::writeJsonStringProperty(stream, "type", "asset");
        stream << ',';
        foundation::writeJsonStringProperty(stream, "assetId", typedPayload.assetId.value());
      } else if constexpr (std::is_same_v<Payload, timeline::NotePayload>) {
        foundation::writeJsonStringProperty(stream, "type", "note");
        stream << ',';
        foundation::writeJsonStringProperty(stream, "title", typedPayload.title);
        stream << ',';
        foundation::writeJsonStringProperty(stream, "markdown", typedPayload.markdown);
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
    foundation::writeJsonStringProperty(stream, "id", node.id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "kind", nodeKindName(node.kind));
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
    foundation::writeJsonStringProperty(stream, "id", edge.id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "kind", edgeKindName(edge.kind));
    stream << ',';
    foundation::writeJsonStringProperty(stream, "sourceNodeId", edge.sourceNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "sourcePort", edge.sourcePort.value);
    stream << ',';
    foundation::writeJsonStringProperty(stream, "targetNodeId", edge.targetNodeId.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "targetPort", edge.targetPort.value);
    stream << ",\"order\":" << edge.order;
    stream << ",\"enabled\":" << (edge.enabled ? "true" : "false");
    stream << '}';
  }
  stream << "]}";

  return stream.str();
}

} // namespace grapple::graph
