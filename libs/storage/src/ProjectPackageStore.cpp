#include <grapple/storage/ProjectPackageStore.hpp>

#include <grapple/project/ProjectSerializer.hpp>

namespace grapple::storage {

ProjectPackageStore::ProjectPackageStore(ProjectPackage package)
  : state_{std::move(package), std::nullopt, {}, {}, {}, std::nullopt} {}

foundation::Result<void> ProjectPackageStore::commit(const AtomicProjectCommit& commit) {
  if (commit.document.info.id != state_.package.projectId) {
    return foundation::Error{"storage.project_id_mismatch", "Committed document must match package project id."};
  }

  if (commit.command.projectId != state_.package.projectId) {
    return foundation::Error{"storage.command_project_id_mismatch", "Command record must match package project id."};
  }

  if (commit.command.afterRevision != commit.document.revision) {
    return foundation::Error{"storage.command_revision_mismatch", "Command record after revision must match committed document revision."};
  }

  if (state_.head.has_value() && commit.command.beforeRevision != state_.head->currentRevision) {
    return foundation::Error{"storage.command_before_revision_mismatch", "Command record before revision must match current package head."};
  }

  ProjectPackageState next = state_;
  next.document = commit.document;

  auto commandAppend = next.commandLog.append(commit.command);
  if (!commandAppend) {
    return commandAppend;
  }

  for (const history::EventRecord& event : commit.events) {
    if (event.projectId != state_.package.projectId) {
      return foundation::Error{"storage.event_project_id_mismatch", "Event record must match package project id."};
    }

    if (event.revision != commit.document.revision) {
      return foundation::Error{"storage.event_revision_mismatch", "Event record revision must match committed document revision."};
    }

    auto eventAppend = next.eventLog.append(event);
    if (!eventAppend) {
      return eventAppend;
    }
  }

  std::optional<foundation::SnapshotId> lastSnapshotId;
  if (commit.snapshot.has_value()) {
    if (commit.snapshot->projectId != state_.package.projectId) {
      return foundation::Error{"storage.snapshot_project_id_mismatch", "Snapshot record must match package project id."};
    }

    if (commit.snapshot->revision != commit.document.revision) {
      return foundation::Error{"storage.snapshot_revision_mismatch", "Snapshot revision must match committed document revision."};
    }

    const foundation::Hash256 expectedHash = project::hashProjectSnapshot(project::ProjectSnapshot{commit.document});
    if (commit.snapshot->canonicalHash != expectedHash) {
      return foundation::Error{"storage.snapshot_hash_mismatch", "Snapshot hash must match committed document canonical hash."};
    }

    auto snapshotAppend = next.snapshots.append(*commit.snapshot);
    if (!snapshotAppend) {
      return snapshotAppend;
    }
    lastSnapshotId = commit.snapshot->id;
  } else if (next.head.has_value()) {
    lastSnapshotId = next.head->lastSnapshotId;
  }

  next.head = history::HistoryHead{
    commit.document.revision,
    commit.command.id,
    lastSnapshotId
  };

  state_ = std::move(next);
  return {};
}

const ProjectPackageState& ProjectPackageStore::state() const noexcept {
  return state_;
}

} // namespace grapple::storage
