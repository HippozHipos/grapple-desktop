#pragma once

#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/graph/GraphDocument.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <cstdlib>
#include <variant>

namespace grapple::project::invariant {

struct ContainingTrackPayloadErrors {
  const char* missingCode;
  const char* missingMessage;
  const char* invalidSourceCode;
  const char* invalidSourceMessage;
  const char* invalidPayloadCode;
  const char* invalidPayloadMessage;
};

inline timeline::ClipKind clipKindForAssetMediaType(asset::AssetMediaType mediaType) {
  switch (mediaType) {
    case asset::AssetMediaType::Video:
      return timeline::ClipKind::Video;
    case asset::AssetMediaType::Audio:
      return timeline::ClipKind::Audio;
    case asset::AssetMediaType::Image:
      return timeline::ClipKind::Image;
  }

  std::abort();
}

inline timeline::TrackKind trackKindForClipKind(timeline::ClipKind clipKind) {
  switch (clipKind) {
    case timeline::ClipKind::Video:
    case timeline::ClipKind::Image:
      return timeline::TrackKind::Visual;
    case timeline::ClipKind::Audio:
      return timeline::TrackKind::Audio;
  }

  std::abort();
}

inline foundation::Result<const timeline::TrackPayload*> requireContainingTrackPayload(
  const graph::GraphDocument& graph,
  const foundation::NodeId& clipNodeId,
  const ContainingTrackPayloadErrors& errors
) {
  for (const graph::GraphEdge& edge : graph.edges()) {
    if (!edge.enabled || edge.kind != graph::EdgeKind::Contains || edge.targetNodeId != clipNodeId) {
      continue;
    }

    const graph::GraphNode* track = graph.findNode(edge.sourceNodeId);
    if (track == nullptr || track->kind != graph::NodeKind::Track) {
      return foundation::Error{errors.invalidSourceCode, errors.invalidSourceMessage};
    }
    const auto* payload = std::get_if<timeline::TrackPayload>(&track->payload);
    if (payload == nullptr) {
      return foundation::Error{errors.invalidPayloadCode, errors.invalidPayloadMessage};
    }
    return payload;
  }

  return foundation::Error{errors.missingCode, errors.missingMessage};
}

inline foundation::Result<void> requireClipMatchesAssetMediaType(
  const timeline::ClipPayload& payload,
  const asset::Asset& asset,
  const char* code,
  const char* message
) {
  if (payload.kind != clipKindForAssetMediaType(asset.metadata.mediaType)) {
    return foundation::Error{code, message};
  }
  return {};
}

inline foundation::Result<void> requireClipMatchesTrackKind(
  const timeline::ClipPayload& clip,
  const timeline::TrackPayload& track,
  const char* code,
  const char* message
) {
  if (track.kind != trackKindForClipKind(clip.kind)) {
    return foundation::Error{code, message};
  }
  return {};
}

} // namespace grapple::project::invariant
