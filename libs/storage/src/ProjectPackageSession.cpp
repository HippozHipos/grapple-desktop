#include <grapple/storage/ProjectPackageSession.hpp>

#include <utility>

namespace grapple::storage {

ProjectPackageSession::ProjectPackageSession(project::ProjectDocument document, ProjectPackage package)
  : controller_{std::move(document)},
    store_{std::move(package)} {}

foundation::Result<ProjectPackageSessionResult> ProjectPackageSession::applyAndCommit(
  const project::ProjectCommandEnvelope& command,
  ProjectCommitRecordOptions options
) {
  project::ProjectController candidate = controller_;

  auto commandResult = candidate.apply(command);
  if (!commandResult) {
    return commandResult.error();
  }

  auto committedSnapshot = candidate.snapshot();
  if (!committedSnapshot) {
    return committedSnapshot.error();
  }

  auto commitResult = store_.commit(makeAtomicProjectCommit(
    committedSnapshot.value(),
    command,
    commandResult.value(),
    std::move(options)
  ));
  if (!commitResult) {
    return commitResult.error();
  }

  controller_ = std::move(candidate);
  return ProjectPackageSessionResult{
    std::move(commandResult.value()),
    std::move(committedSnapshot.value())
  };
}

foundation::Result<project::ProjectSnapshot> ProjectPackageSession::snapshot() const {
  return controller_.snapshot();
}

const ProjectPackageState& ProjectPackageSession::packageState() const noexcept {
  return store_.state();
}

} // namespace grapple::storage
