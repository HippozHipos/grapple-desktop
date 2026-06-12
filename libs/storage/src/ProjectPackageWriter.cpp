#include <grapple/storage/ProjectPackageWriter.hpp>

#include <grapple/project/ProjectSerializer.hpp>

#include <filesystem>
#include <fstream>

namespace grapple::storage {

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
