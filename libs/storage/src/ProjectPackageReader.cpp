#include <grapple/storage/ProjectPackageReader.hpp>

#include <grapple/project/ProjectSerializer.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace grapple::storage {

namespace {

foundation::Result<std::string> readTextFile(const std::filesystem::path& path, const char* errorCode) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return foundation::Error{errorCode, "Could not open " + path.lexically_normal().string() + "."};
  }

  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return foundation::Error{errorCode, "Could not read " + path.lexically_normal().string() + "."};
  }
  return contents.str();
}

foundation::Result<void> validateManifestForPackage(
  const ProjectPackageManifest& manifest,
  const ProjectPackage& package
) {
  if (manifest.projectId != package.projectId) {
    return foundation::Error{"storage.package_manifest_project_id_mismatch", "Package manifest project id must match package."};
  }
  if (manifest.schemaVersion != package.schemaVersion) {
    return foundation::Error{"storage.package_manifest_schema_mismatch", "Package manifest schema version must match package."};
  }
  return {};
}

foundation::Result<void> validateLatestSnapshot(
  const project::ProjectSnapshot& snapshot,
  const ProjectPackageSnapshotManifest& snapshotManifest,
  const ProjectPackage& package
) {
  if (snapshot.info.id != package.projectId) {
    return foundation::Error{"storage.package_snapshot_project_id_mismatch", "Latest snapshot project id must match package."};
  }
  if (snapshot.revision != snapshotManifest.revision) {
    return foundation::Error{"storage.package_snapshot_revision_mismatch", "Latest snapshot revision must match manifest."};
  }
  if (!(snapshot.canonicalHash == snapshotManifest.canonicalHash)) {
    return foundation::Error{"storage.package_snapshot_hash_mismatch", "Latest snapshot hash must match manifest."};
  }
  return {};
}

} // namespace

foundation::Result<ProjectPackageManifest> ProjectPackageReader::readManifest(const ProjectPackage& package) const {
  if (package.rootPath.value.empty()) {
    return foundation::Error{"storage.package_root_empty", "Project package root path must not be empty."};
  }

  const std::filesystem::path manifestPath = std::filesystem::path{package.rootPath.value} / "manifest.json";
  auto contents = readTextFile(manifestPath, "storage.manifest_open_failed");
  if (!contents) {
    return contents.error();
  }

  auto manifest = deserializeCanonicalProjectPackageManifest(contents.value());
  if (!manifest) {
    return manifest.error();
  }

  auto valid = validateManifestForPackage(manifest.value(), package);
  if (!valid) {
    return valid.error();
  }
  return manifest.value();
}

foundation::Result<ProjectPackageLatestSnapshot> ProjectPackageReader::readLatestSnapshot(
  const ProjectPackage& package
) const {
  auto manifest = readManifest(package);
  if (!manifest) {
    return manifest.error();
  }
  if (!manifest.value().latestSnapshot.has_value()) {
    return foundation::Error{"storage.package_latest_snapshot_missing", "Package manifest does not reference a latest snapshot."};
  }

  const ProjectPackageSnapshotManifest& latestSnapshot = *manifest.value().latestSnapshot;
  const std::filesystem::path relativeDocumentPath{latestSnapshot.documentPath.value};
  if (relativeDocumentPath.empty()) {
    return foundation::Error{"storage.snapshot_document_path_empty", "Latest snapshot document path must not be empty."};
  }
  if (relativeDocumentPath.is_absolute()) {
    return foundation::Error{"storage.snapshot_document_path_absolute", "Latest snapshot document path must be package-relative."};
  }

  const std::filesystem::path snapshotPath = std::filesystem::path{package.rootPath.value} / relativeDocumentPath;
  auto contents = readTextFile(snapshotPath, "storage.snapshot_open_failed");
  if (!contents) {
    return contents.error();
  }

  auto snapshot = project::deserializeCanonicalProjectSnapshot(contents.value());
  if (!snapshot) {
    return snapshot.error();
  }
  auto valid = validateLatestSnapshot(snapshot.value(), latestSnapshot, package);
  if (!valid) {
    return valid.error();
  }

  return ProjectPackageLatestSnapshot{
    manifest.value(),
    snapshot.value()
  };
}

} // namespace grapple::storage
