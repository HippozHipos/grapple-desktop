#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectCommandResult.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/storage/ProjectPackageStore.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace grapple::storage {

struct SnapshotCommitRecord {
  foundation::SnapshotId id;
  foundation::FilePath documentPath;
  std::optional<std::string> label;
};

struct ProjectCommitRecordOptions {
  std::chrono::system_clock::time_point createdAt;
  std::optional<SnapshotCommitRecord> snapshot;
};

[[nodiscard]] AtomicProjectCommit makeAtomicProjectCommit(
  project::ProjectSnapshot projectSnapshot,
  const project::ProjectCommandEnvelope& command,
  const project::ProjectCommandResult& result,
  ProjectCommitRecordOptions options
);

} // namespace grapple::storage
