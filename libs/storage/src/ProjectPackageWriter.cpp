#include <grapple/storage/ProjectPackageWriter.hpp>

#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>

#include <filesystem>
#include <fstream>

namespace grapple::storage {

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

  const std::filesystem::path packageRoot{package.rootPath.value};
  std::error_code directoryError;
  std::filesystem::create_directories(packageRoot, directoryError);
  if (directoryError) {
    return foundation::Error{"storage.package_directory_create_failed", directoryError.message()};
  }

  const std::filesystem::path manifestPath = packageRoot / "manifest.json";
  std::ofstream output{manifestPath, std::ios::binary | std::ios::trunc};
  if (!output) {
    return foundation::Error{"storage.manifest_open_failed", "Could not open package manifest for writing."};
  }

  output << serializeCanonicalProjectPackageManifest(manifest);
  if (!output) {
    return foundation::Error{"storage.manifest_write_failed", "Could not write package manifest."};
  }

  return foundation::FilePath{manifestPath.lexically_normal().string()};
}

foundation::Result<foundation::FilePath> ProjectPackageWriter::writeSnapshot(
  const ProjectSnapshotWriteRequest& request
) const {
  if (request.package.rootPath.value.empty()) {
    return foundation::Error{"storage.package_root_empty", "Project package root path must not be empty."};
  }

  if (request.snapshot.info.id != request.package.projectId) {
    return foundation::Error{"storage.snapshot_project_id_mismatch", "Snapshot project id must match package project id."};
  }

  if (request.snapshotRecord.documentPath.value.empty()) {
    return foundation::Error{"storage.snapshot_document_path_empty", "Snapshot document path must not be empty."};
  }

  const std::filesystem::path relativeDocumentPath{request.snapshotRecord.documentPath.value};
  if (relativeDocumentPath.is_absolute()) {
    return foundation::Error{"storage.snapshot_document_path_absolute", "Snapshot document path must be package-relative."};
  }

  const std::filesystem::path packageRoot{request.package.rootPath.value};
  const std::filesystem::path documentPath = packageRoot / relativeDocumentPath;
  const std::filesystem::path parentPath = documentPath.parent_path();
  if (!parentPath.empty()) {
    std::error_code directoryError;
    std::filesystem::create_directories(parentPath, directoryError);
    if (directoryError) {
      return foundation::Error{"storage.snapshot_directory_create_failed", directoryError.message()};
    }
  }

  std::ofstream output{documentPath, std::ios::binary | std::ios::trunc};
  if (!output) {
    return foundation::Error{"storage.snapshot_open_failed", "Could not open snapshot document for writing."};
  }

  output << project::serializeCanonicalProjectSnapshot(request.snapshot);
  if (!output) {
    return foundation::Error{"storage.snapshot_write_failed", "Could not write snapshot document."};
  }

  return foundation::FilePath{documentPath.lexically_normal().string()};
}

} // namespace grapple::storage
