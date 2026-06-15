#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectCommandResult.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/storage/ProjectPackageStore.hpp>

namespace grapple::storage {

struct ProjectPackageSessionResult {
  project::ProjectCommandResult commandResult;
  project::ProjectSnapshot snapshot;
};

class ProjectPackageSession {
public:
  ProjectPackageSession(project::ProjectDocument document, ProjectPackage package);
  ProjectPackageSession(project::ProjectDocument document, ProjectPackageState state);

  static foundation::Result<ProjectPackageSession> open(ProjectPackage package);

  foundation::Result<ProjectPackageSessionResult> applyAndCommit(
    const project::ProjectCommandEnvelope& command,
    ProjectCommitRecordOptions options
  );
  foundation::Result<void> retargetPackage(ProjectPackage package);

  [[nodiscard]] foundation::Result<project::ProjectSnapshot> snapshot() const;
  [[nodiscard]] const project::ProjectSnapshot* findCommittedSnapshot(
    foundation::RevisionId revision
  ) const noexcept;
  [[nodiscard]] const ProjectPackageState& packageState() const noexcept;

private:
  project::ProjectController controller_;
  ProjectPackageStore store_;
};

} // namespace grapple::storage
