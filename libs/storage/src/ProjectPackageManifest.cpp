#include <grapple/storage/ProjectPackageManifest.hpp>

#include <grapple/foundation/Json.hpp>

#include <sstream>

namespace grapple::storage {

namespace {

void writeOptionalId(std::ostringstream& stream, const char* key, const std::optional<foundation::CommandId>& id) {
  stream << foundation::jsonQuoted(key) << ':';
  if (id.has_value()) {
    stream << foundation::jsonQuoted(id->value());
  } else {
    stream << "null";
  }
}

void writeOptionalId(std::ostringstream& stream, const char* key, const std::optional<foundation::SnapshotId>& id) {
  stream << foundation::jsonQuoted(key) << ':';
  if (id.has_value()) {
    stream << foundation::jsonQuoted(id->value());
  } else {
    stream << "null";
  }
}

const history::SnapshotRecord* findSnapshotById(
  const history::SnapshotStore& snapshots,
  const foundation::SnapshotId& id
) {
  for (const history::SnapshotRecord& snapshot : snapshots.records()) {
    if (snapshot.id == id) {
      return &snapshot;
    }
  }
  return nullptr;
}

} // namespace

foundation::Result<ProjectPackageManifest> buildProjectPackageManifest(const ProjectPackageState& state) {
  if (!state.package.projectId) {
    return foundation::Error{"storage.package_project_id_empty", "Project package id must not be empty."};
  }

  ProjectPackageManifest manifest;
  manifest.projectId = state.package.projectId;
  manifest.schemaVersion = state.package.schemaVersion;

  if (!state.head.has_value()) {
    return manifest;
  }

  manifest.head = ProjectPackageHeadManifest{
    state.head->currentRevision,
    state.head->lastCommandId,
    state.head->lastSnapshotId
  };

  if (!state.head->lastSnapshotId.has_value()) {
    return manifest;
  }

  const history::SnapshotRecord* snapshot = findSnapshotById(state.snapshots, *state.head->lastSnapshotId);
  if (snapshot == nullptr) {
    return foundation::Error{"storage.package_head_snapshot_missing", "Package head references a missing snapshot record."};
  }

  manifest.latestSnapshot = ProjectPackageSnapshotManifest{
    snapshot->id,
    snapshot->revision,
    snapshot->canonicalHash,
    snapshot->documentPath,
    snapshot->label
  };
  return manifest;
}

std::string serializeCanonicalProjectPackageManifest(const ProjectPackageManifest& manifest) {
  std::ostringstream stream;
  stream << '{';
  stream << "\"schemaVersion\":" << manifest.schemaVersion;
  stream << ',';
  foundation::writeJsonStringProperty(stream, "projectId", manifest.projectId.value());
  stream << ",\"head\":";
  if (manifest.head.has_value()) {
    stream << '{';
    foundation::writeJsonStringProperty(stream, "revision", manifest.head->revision.value());
    stream << ',';
    writeOptionalId(stream, "lastCommandId", manifest.head->lastCommandId);
    stream << ',';
    writeOptionalId(stream, "lastSnapshotId", manifest.head->lastSnapshotId);
    stream << '}';
  } else {
    stream << "null";
  }

  stream << ",\"latestSnapshot\":";
  if (manifest.latestSnapshot.has_value()) {
    stream << '{';
    foundation::writeJsonStringProperty(stream, "id", manifest.latestSnapshot->id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "revision", manifest.latestSnapshot->revision.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "canonicalHash", manifest.latestSnapshot->canonicalHash.toHex());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "documentPath", manifest.latestSnapshot->documentPath.value);
    stream << ",\"label\":";
    if (manifest.latestSnapshot->label.has_value()) {
      stream << foundation::jsonQuoted(*manifest.latestSnapshot->label);
    } else {
      stream << "null";
    }
    stream << '}';
  } else {
    stream << "null";
  }
  stream << '}';
  return stream.str();
}

} // namespace grapple::storage
