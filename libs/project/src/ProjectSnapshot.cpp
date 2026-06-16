#include <grapple/project/ProjectSnapshot.hpp>

#include "internal/ProjectInvariants.hpp"

#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/timeline/Payloads.hpp>

namespace grapple::project {

ProjectSnapshot makeProjectSnapshot(const ProjectDocument& document) {
  ProjectSnapshot snapshot{
    document.info,
    document.revision,
    document.revisionNumber,
    document.settings,
    document.assets,
    document.graph,
    foundation::Hash256{}
  };
  snapshot.canonicalHash = hashProjectSnapshot(snapshot);
  return snapshot;
}

ProjectDocument makeProjectDocument(const ProjectSnapshot& snapshot) {
  return ProjectDocument{
    snapshot.info,
    snapshot.revision,
    snapshot.revisionNumber,
    snapshot.settings,
    snapshot.assets,
    snapshot.graph
  };
}

foundation::Result<void> validateProjectSnapshotReferences(const ProjectSnapshot& snapshot) {
  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    if (node.kind != graph::NodeKind::Clip) {
      continue;
    }
    auto trackPayload = invariant::requireContainingTrackPayload(
      snapshot.graph,
      node.id,
      invariant::ContainingTrackPayloadErrors{
        "project.snapshot_clip_track_missing",
        "Snapshot clip nodes must be contained by a track.",
        "project.snapshot_clip_track_invalid",
        "Snapshot clip containment source must be a track.",
        "project.snapshot_track_payload_invalid",
        "Snapshot track nodes must carry track payloads."
      }
    );
    if (!trackPayload) {
      return trackPayload.error();
    }
    if (const auto* payload = std::get_if<timeline::ClipPayload>(&node.payload)) {
      auto trackKind = invariant::requireClipMatchesTrackKind(
        *payload,
        *trackPayload.value(),
        "project.snapshot_clip_track_kind_mismatch",
        "Snapshot clip kind must match its containing track kind."
      );
      if (!trackKind) {
        return trackKind.error();
      }
      const asset::Asset* asset = snapshot.assets.find(payload->assetId);
      if (asset == nullptr) {
        return foundation::Error{"project.snapshot_clip_asset_missing", "Snapshot clip assets must exist in the snapshot asset catalog."};
      }
      auto assetKind = invariant::requireClipMatchesAssetMediaType(
        *payload,
        *asset,
        "project.snapshot_clip_asset_kind_mismatch",
        "Snapshot clip kind must match the referenced asset media type."
      );
      if (!assetKind) {
        return assetKind.error();
      }
      continue;
    }
    if (std::holds_alternative<timeline::TextClipPayload>(node.payload)) {
      if (trackPayload.value()->kind != timeline::TrackKind::Visual) {
        return foundation::Error{"project.snapshot_text_clip_track_kind_mismatch", "Snapshot text clips must be contained by visual tracks."};
      }
      continue;
    }
    return foundation::Error{"project.snapshot_clip_payload_invalid", "Snapshot clip nodes must carry clip payloads."};
  }
  return {};
}

} // namespace grapple::project
