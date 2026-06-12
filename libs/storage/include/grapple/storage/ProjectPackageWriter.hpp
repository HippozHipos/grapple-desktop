#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/storage/ProjectPackage.hpp>

namespace grapple::storage {

struct ProjectSnapshotWriteRequest {
  ProjectPackage package;
  project::ProjectSnapshot snapshot;
  SnapshotCommitRecord snapshotRecord;
};

class ProjectPackageWriter {
public:
  foundation::Result<foundation::FilePath> writeSnapshot(const ProjectSnapshotWriteRequest& request) const;
};

} // namespace grapple::storage
