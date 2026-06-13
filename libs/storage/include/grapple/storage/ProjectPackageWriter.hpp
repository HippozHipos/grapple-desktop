#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/history/CommandLogStore.hpp>
#include <grapple/history/EventLogStore.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/storage/ProjectPackage.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/SchemaMigration.hpp>

namespace grapple::storage {

struct ProjectSnapshotWriteRequest {
  ProjectPackage package;
  project::ProjectSnapshot snapshot;
  SnapshotCommitRecord snapshotRecord;
};

struct ProjectCommandLogWriteRequest {
  ProjectPackage package;
  foundation::FilePath commandLogPath;
  history::CommandLogStore commandLog;
};

struct ProjectEventLogWriteRequest {
  ProjectPackage package;
  foundation::FilePath eventLogPath;
  history::EventLogStore eventLog;
};

struct ProjectSchemaMigrationLogWriteRequest {
  ProjectPackage package;
  foundation::FilePath schemaMigrationLogPath;
  SchemaMigrationLog schemaMigrationLog;
};

class ProjectPackageWriter {
public:
  foundation::Result<foundation::FilePath> writeManifest(const ProjectPackageManifest& manifest, const ProjectPackage& package) const;
  foundation::Result<foundation::FilePath> writeSnapshot(const ProjectSnapshotWriteRequest& request) const;
  foundation::Result<foundation::FilePath> writeCommandLog(const ProjectCommandLogWriteRequest& request) const;
  foundation::Result<foundation::FilePath> writeEventLog(const ProjectEventLogWriteRequest& request) const;
  foundation::Result<foundation::FilePath> writeSchemaMigrationLog(const ProjectSchemaMigrationLogWriteRequest& request) const;
};

} // namespace grapple::storage
