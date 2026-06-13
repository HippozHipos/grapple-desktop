#include <grapple/project/ProjectSnapshot.hpp>

#include "ProjectInvariants.hpp"

#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <variant>

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
    const auto* payload = std::get_if<timeline::ClipPayload>(&node.payload);
    if (payload == nullptr) {
      return foundation::Error{"project.snapshot_clip_payload_invalid", "Snapshot clip nodes must carry clip payloads."};
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
  }
  return {};
}

} // namespace grapple::project
