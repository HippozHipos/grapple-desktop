#include <grapple/storage/ProjectPackageWriter.hpp>

#include <grapple/history/HistorySerializer.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>

#include <filesystem>
#include <fstream>

namespace grapple::storage {

namespace {

foundation::Result<foundation::FilePath> writePackageTextFile(
  const ProjectPackage& package,
  const foundation::FilePath& relativePath,
  const std::string& contents,
  const char* emptyPathCode,
  const char* absolutePathCode,
  const char* directoryCode,
  const char* openCode,
  const char* writeCode
) {
  if (package.rootPath.value.empty()) {
    return foundation::Error{"storage.package_root_empty", "Project package root path must not be empty."};
  }

  if (relativePath.value.empty()) {
    return foundation::Error{emptyPathCode, "Package file path must not be empty."};
  }

  const std::filesystem::path packageRelativePath{relativePath.value};
  if (packageRelativePath.is_absolute()) {
    return foundation::Error{absolutePathCode, "Package file path must be package-relative."};
  }

  const std::filesystem::path documentPath = std::filesystem::path{package.rootPath.value} / packageRelativePath;
  const std::filesystem::path parentPath = documentPath.parent_path();
  if (!parentPath.empty()) {
    std::error_code directoryError;
    std::filesystem::create_directories(parentPath, directoryError);
    if (directoryError) {
      return foundation::Error{directoryCode, directoryError.message()};
    }
  }

  std::ofstream output{documentPath, std::ios::binary | std::ios::trunc};
  if (!output) {
    return foundation::Error{openCode, "Could not open package file for writing."};
  }

  output << contents;
  if (!output) {
    return foundation::Error{writeCode, "Could not write package file."};
  }

  return foundation::FilePath{documentPath.lexically_normal().string()};
}

} // namespace

foundation::Result<foundation::FilePath> ProjectPackageWriter::writeManifest(
  const ProjectPackageManifest& manifest,
  const ProjectPackage& package
) const {
  if (package.rootPath.value.empty()) {
    return foundation::Error{"storage.package_root_empty", "Project package root path must not be empty."};
  }

  if (manifest.projectId != package.projectId) {
    return foundation::Error{"storage.manifest_project_id_mismatch", "Manifest project id must match package project id."};
  }

  if (manifest.schemaVersion != package.schemaVersion) {
    return foundation::Error{"storage.manifest_schema_mismatch", "Manifest schema version must match package schema version."};
  }

  return writePackageTextFile(
    package,
    foundation::FilePath{"manifest.json"},
    serializeCanonicalProjectPackageManifest(manifest),
    "storage.manifest_path_empty",
    "storage.manifest_path_absolute",
    "storage.package_directory_create_failed",
    "storage.manifest_open_failed",
    "storage.manifest_write_failed"
  );
}

foundation::Result<foundation::FilePath> ProjectPackageWriter::writeSnapshot(
  const ProjectSnapshotWriteRequest& request
) const {
  if (request.snapshot.info.id != request.package.projectId) {
    return foundation::Error{"storage.snapshot_project_id_mismatch", "Snapshot project id must match package project id."};
  }

  return writePackageTextFile(
    request.package,
    request.snapshotRecord.documentPath,
    project::serializeCanonicalProjectSnapshot(request.snapshot),
    "storage.snapshot_document_path_empty",
    "storage.snapshot_document_path_absolute",
    "storage.snapshot_directory_create_failed",
    "storage.snapshot_open_failed",
    "storage.snapshot_write_failed"
  );
}

foundation::Result<foundation::FilePath> ProjectPackageWriter::writeCommandLog(
  const ProjectCommandLogWriteRequest& request
) const {
  return writePackageTextFile(
    request.package,
    request.commandLogPath,
    history::serializeCanonicalCommandLog(request.commandLog),
    "storage.command_log_path_empty",
    "storage.command_log_path_absolute",
    "storage.command_log_directory_create_failed",
    "storage.command_log_open_failed",
    "storage.command_log_write_failed"
  );
}

foundation::Result<foundation::FilePath> ProjectPackageWriter::writeEventLog(
  const ProjectEventLogWriteRequest& request
) const {
  return writePackageTextFile(
    request.package,
    request.eventLogPath,
    history::serializeCanonicalEventLog(request.eventLog),
    "storage.event_log_path_empty",
    "storage.event_log_path_absolute",
    "storage.event_log_directory_create_failed",
    "storage.event_log_open_failed",
    "storage.event_log_write_failed"
  );
}

foundation::Result<foundation::FilePath> ProjectPackageWriter::writeSchemaMigrationLog(
  const ProjectSchemaMigrationLogWriteRequest& request
) const {
  return writePackageTextFile(
    request.package,
    request.schemaMigrationLogPath,
    serializeCanonicalSchemaMigrationLog(request.schemaMigrationLog),
    "storage.schema_migration_log_path_empty",
    "storage.schema_migration_log_path_absolute",
    "storage.schema_migration_log_directory_create_failed",
    "storage.schema_migration_log_open_failed",
    "storage.schema_migration_log_write_failed"
  );
}

} // namespace grapple::storage
