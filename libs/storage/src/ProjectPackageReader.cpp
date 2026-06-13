#include <grapple/storage/ProjectPackageReader.hpp>

#include <grapple/history/HistorySerializer.hpp>
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

foundation::Result<std::filesystem::path> packageRelativePath(
  const ProjectPackage& package,
  const foundation::FilePath& relativePath,
  const char* emptyPathCode,
  const char* absolutePathCode
) {
  if (relativePath.value.empty()) {
    return foundation::Error{emptyPathCode, "Package file path must not be empty."};
  }
  const std::filesystem::path packageRelativePath{relativePath.value};
  if (packageRelativePath.is_absolute()) {
    return foundation::Error{absolutePathCode, "Package file path must be package-relative."};
  }
  return std::filesystem::path{package.rootPath.value} / packageRelativePath;
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
  auto references = project::validateProjectSnapshotReferences(snapshot);
  if (!references) {
    return references.error();
  }
  return {};
}

} // namespace

foundation::Result<ProjectPackageManifest> ProjectPackageReader::readManifestAtRoot(const foundation::FilePath& rootPath) const {
  if (rootPath.value.empty()) {
    return foundation::Error{"storage.package_root_empty", "Project package root path must not be empty."};
  }

  const std::filesystem::path manifestPath = std::filesystem::path{rootPath.value} / "manifest.json";
  auto contents = readTextFile(manifestPath, "storage.manifest_open_failed");
  if (!contents) {
    return contents.error();
  }

  auto manifest = deserializeCanonicalProjectPackageManifest(contents.value());
  if (!manifest) {
    return manifest.error();
  }

  return manifest.value();
}

foundation::Result<ProjectPackage> ProjectPackageReader::readPackage(foundation::FilePath rootPath) const {
  auto manifest = readManifestAtRoot(rootPath);
  if (!manifest) {
    return manifest.error();
  }

  return ProjectPackage{
    manifest.value().projectId,
    std::move(rootPath),
    manifest.value().schemaVersion
  };
}

foundation::Result<ProjectPackageManifest> ProjectPackageReader::readManifest(const ProjectPackage& package) const {
  auto manifest = readManifestAtRoot(package.rootPath);
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
  auto snapshotPath = packageRelativePath(
    package,
    latestSnapshot.documentPath,
    "storage.snapshot_document_path_empty",
    "storage.snapshot_document_path_absolute"
  );
  if (!snapshotPath) {
    return snapshotPath.error();
  }

  auto contents = readTextFile(snapshotPath.value(), "storage.snapshot_open_failed");
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

foundation::Result<ProjectPackageHistoryLogs> ProjectPackageReader::readHistoryLogs(
  const ProjectPackage& package
) const {
  auto manifest = readManifest(package);
  if (!manifest) {
    return manifest.error();
  }

  auto commandLogPath = packageRelativePath(
    package,
    manifest.value().commandLogPath,
    "storage.command_log_path_empty",
    "storage.command_log_path_absolute"
  );
  if (!commandLogPath) {
    return commandLogPath.error();
  }
  auto eventLogPath = packageRelativePath(
    package,
    manifest.value().eventLogPath,
    "storage.event_log_path_empty",
    "storage.event_log_path_absolute"
  );
  if (!eventLogPath) {
    return eventLogPath.error();
  }

  auto commandLogContents = readTextFile(commandLogPath.value(), "storage.command_log_open_failed");
  if (!commandLogContents) {
    return commandLogContents.error();
  }
  auto eventLogContents = readTextFile(eventLogPath.value(), "storage.event_log_open_failed");
  if (!eventLogContents) {
    return eventLogContents.error();
  }

  auto commandLog = history::deserializeCanonicalCommandLog(commandLogContents.value());
  if (!commandLog) {
    return commandLog.error();
  }
  auto eventLog = history::deserializeCanonicalEventLog(eventLogContents.value());
  if (!eventLog) {
    return eventLog.error();
  }

  return ProjectPackageHistoryLogs{
    manifest.value(),
    commandLog.value(),
    eventLog.value()
  };
}

} // namespace grapple::storage
