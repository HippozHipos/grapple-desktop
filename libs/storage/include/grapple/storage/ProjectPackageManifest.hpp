#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/storage/ProjectPackageStore.hpp>

#include <optional>
#include <string>

namespace grapple::storage {

struct ProjectPackageSnapshotManifest {
  foundation::SnapshotId id;
  foundation::RevisionId revision;
  foundation::Hash256 canonicalHash;
  foundation::FilePath documentPath;
  std::optional<std::string> label;
};

struct ProjectPackageHeadManifest {
  foundation::RevisionId revision;
  std::optional<foundation::CommandId> lastCommandId;
  std::optional<foundation::SnapshotId> lastSnapshotId;
};

struct ProjectPackageManifest {
  foundation::ProjectId projectId;
  int schemaVersion = 1;
  std::optional<ProjectPackageHeadManifest> head;
  std::optional<ProjectPackageSnapshotManifest> latestSnapshot;
};

foundation::Result<ProjectPackageManifest> buildProjectPackageManifest(const ProjectPackageState& state);
std::string serializeCanonicalProjectPackageManifest(const ProjectPackageManifest& manifest);

} // namespace grapple::storage
