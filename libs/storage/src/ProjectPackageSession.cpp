#include <grapple/storage/ProjectPackageSession.hpp>

#include <grapple/storage/ProjectPackageReader.hpp>

#include <chrono>
#include <utility>

namespace grapple::storage {

ProjectPackageSession::ProjectPackageSession(project::ProjectDocument document, ProjectPackage package)
  : controller_{std::move(document)},
    store_{std::move(package)} {}

ProjectPackageSession::ProjectPackageSession(project::ProjectDocument document, ProjectPackageState state)
  : controller_{std::move(document)},
    store_{std::move(state)} {}

foundation::Result<ProjectPackageSession> ProjectPackageSession::open(ProjectPackage package) {
  const ProjectPackageReader reader;
  auto latestSnapshot = reader.readLatestSnapshot(package);
  if (!latestSnapshot) {
    return latestSnapshot.error();
  }
  auto historyLogs = reader.readHistoryLogs(package);
  if (!historyLogs) {
    return historyLogs.error();
  }

  if (serializeCanonicalProjectPackageManifest(latestSnapshot.value().manifest) !=
      serializeCanonicalProjectPackageManifest(historyLogs.value().manifest)) {
    return foundation::Error{"storage.package_manifest_changed", "Package manifest changed while opening package."};
  }

  ProjectPackageState state;
  state.package = std::move(package);
  state.projectSnapshot = latestSnapshot.value().snapshot;
  state.commandLog = historyLogs.value().commandLog;
  state.eventLog = historyLogs.value().eventLog;

  const ProjectPackageManifest& manifest = latestSnapshot.value().manifest;
  if (manifest.head.has_value()) {
    state.head = history::HistoryHead{
      manifest.head->revision,
      manifest.head->lastCommandId,
      manifest.head->lastSnapshotId
    };
  }
  if (manifest.latestSnapshot.has_value()) {
    const ProjectPackageSnapshotManifest& snapshotManifest = *manifest.latestSnapshot;
    auto snapshotAppend = state.snapshots.append(history::SnapshotRecord{
      snapshotManifest.id,
      manifest.projectId,
      snapshotManifest.revision,
      snapshotManifest.canonicalHash,
      snapshotManifest.documentPath,
      snapshotManifest.label,
      std::chrono::system_clock::time_point{}
    });
    if (!snapshotAppend) {
      return snapshotAppend.error();
    }
  }

  return ProjectPackageSession{
    project::makeProjectDocument(latestSnapshot.value().snapshot),
    std::move(state)
  };
}

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
