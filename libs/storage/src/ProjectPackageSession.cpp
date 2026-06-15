#include <grapple/storage/ProjectPackageSession.hpp>

#include <grapple/storage/ProjectPackageReader.hpp>

#include <chrono>
#include <algorithm>
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
  auto snapshotDocuments = reader.readSnapshotDocuments(package);
  if (!snapshotDocuments) {
    return snapshotDocuments.error();
  }
  auto historyLogs = reader.readHistoryLogs(package);
  if (!historyLogs) {
    return historyLogs.error();
  }

  if (serializeCanonicalProjectPackageManifest(snapshotDocuments.value().manifest) !=
      serializeCanonicalProjectPackageManifest(historyLogs.value().manifest)) {
    return foundation::Error{"storage.package_manifest_changed", "Package manifest changed while opening package."};
  }

  const ProjectPackageManifest& manifest = snapshotDocuments.value().manifest;
  if (!manifest.head.has_value()) {
    return foundation::Error{"storage.package_head_missing", "Package manifest does not contain a project head."};
  }

  const project::ProjectSnapshot* headSnapshot = nullptr;
  for (const project::ProjectSnapshot& snapshot : snapshotDocuments.value().snapshots) {
    if (snapshot.revision == manifest.head->revision) {
      headSnapshot = &snapshot;
      break;
    }
  }
  if (headSnapshot == nullptr) {
    return foundation::Error{"storage.package_head_snapshot_missing", "Package head revision has no snapshot document."};
  }
  const project::ProjectSnapshot headSnapshotDocument = *headSnapshot;

  ProjectPackageState state;
  state.package = std::move(package);
  state.projectSnapshot = headSnapshotDocument;
  state.snapshotDocuments = std::move(snapshotDocuments.value().snapshots);
  state.commandLog = historyLogs.value().commandLog;
  state.eventLog = historyLogs.value().eventLog;
  state.schemaMigrationLog = historyLogs.value().schemaMigrationLog;

  state.head = history::HistoryHead{
    manifest.head->revision,
    manifest.head->lastCommandId,
    manifest.head->lastSnapshotId
  };

  for (const ProjectPackageSnapshotManifest& snapshotManifest : manifest.snapshots) {
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
    project::makeProjectDocument(headSnapshotDocument),
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

foundation::Result<void> ProjectPackageSession::retargetPackage(ProjectPackage package) {
  return store_.retargetPackage(std::move(package));
}

foundation::Result<project::ProjectSnapshot> ProjectPackageSession::snapshot() const {
  return controller_.snapshot();
}

const project::ProjectSnapshot* ProjectPackageSession::findCommittedSnapshot(
  foundation::RevisionId revision
) const noexcept {
  const std::vector<project::ProjectSnapshot>& snapshots = store_.state().snapshotDocuments;
  const auto iterator = std::find_if(snapshots.begin(), snapshots.end(), [&](const project::ProjectSnapshot& snapshot) {
    return snapshot.revision == revision;
  });

  if (iterator == snapshots.end()) {
    return nullptr;
  }
  return &*iterator;
}

const ProjectPackageState& ProjectPackageSession::packageState() const noexcept {
  return store_.state();
}

} // namespace grapple::storage
