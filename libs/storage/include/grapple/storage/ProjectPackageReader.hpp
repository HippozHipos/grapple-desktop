#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/storage/ProjectPackage.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>

namespace grapple::storage {

struct ProjectPackageLatestSnapshot {
  ProjectPackageManifest manifest;
  project::ProjectSnapshot snapshot;
};

class ProjectPackageReader {
public:
  foundation::Result<ProjectPackageManifest> readManifest(const ProjectPackage& package) const;
  foundation::Result<ProjectPackageLatestSnapshot> readLatestSnapshot(const ProjectPackage& package) const;
};

} // namespace grapple::storage
