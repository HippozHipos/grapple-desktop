#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/history/CommandLogStore.hpp>
#include <grapple/history/EventLogStore.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/storage/ProjectPackage.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>

namespace grapple::storage {

struct ProjectPackageLatestSnapshot {
  ProjectPackageManifest manifest;
  project::ProjectSnapshot snapshot;
};

struct ProjectPackageHistoryLogs {
  ProjectPackageManifest manifest;
  history::CommandLogStore commandLog;
  history::EventLogStore eventLog;
};

class ProjectPackageReader {
public:
  foundation::Result<ProjectPackage> readPackage(foundation::FilePath rootPath) const;
  foundation::Result<ProjectPackageManifest> readManifestAtRoot(const foundation::FilePath& rootPath) const;
  foundation::Result<ProjectPackageManifest> readManifest(const ProjectPackage& package) const;
  foundation::Result<ProjectPackageLatestSnapshot> readLatestSnapshot(const ProjectPackage& package) const;
  foundation::Result<ProjectPackageHistoryLogs> readHistoryLogs(const ProjectPackage& package) const;
};

} // namespace grapple::storage
