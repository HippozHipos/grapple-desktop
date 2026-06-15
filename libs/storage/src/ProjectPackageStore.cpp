#include <grapple/storage/ProjectPackageStore.hpp>

namespace grapple::storage {

ProjectPackageStore::ProjectPackageStore(ProjectPackage package)
  : state_{std::move(package), std::nullopt, {}, {}, {}, {}, {}, std::nullopt} {}

ProjectPackageStore::ProjectPackageStore(ProjectPackageState state)
  : state_{std::move(state)} {}

foundation::Result<void> ProjectPackageStore::commit(const AtomicProjectCommit& commit) {
  if (commit.projectSnapshot.info.id != state_.package.projectId) {
    return foundation::Error{"storage.project_id_mismatch", "Committed project snapshot must match package project id."};
  }

  if (commit.command.projectId != state_.package.projectId) {
    return foundation::Error{"storage.command_project_id_mismatch", "Command record must match package project id."};
  }

  if (commit.command.afterRevision != commit.projectSnapshot.revision) {
    return foundation::Error{"storage.command_revision_mismatch", "Command record after revision must match committed project snapshot revision."};
  }

  if (state_.head.has_value() && commit.command.beforeRevision != state_.head->currentRevision) {
    return foundation::Error{"storage.command_before_revision_mismatch", "Command record before revision must match current package head."};
  }

  ProjectPackageState next = state_;
  next.projectSnapshot = commit.projectSnapshot;

  auto commandAppend = next.commandLog.append(commit.command);
  if (!commandAppend) {
    return commandAppend;
  }

  for (const history::EventRecord& event : commit.events) {
    if (event.projectId != state_.package.projectId) {
      return foundation::Error{"storage.event_project_id_mismatch", "Event record must match package project id."};
    }

    if (event.revision != commit.projectSnapshot.revision) {
      return foundation::Error{"storage.event_revision_mismatch", "Event record revision must match committed project snapshot revision."};
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

    if (commit.snapshot->revision != commit.projectSnapshot.revision) {
      return foundation::Error{"storage.snapshot_revision_mismatch", "Snapshot revision must match committed project snapshot revision."};
    }

    const foundation::Hash256 expectedHash = commit.projectSnapshot.canonicalHash;
    if (commit.snapshot->canonicalHash != expectedHash) {
      return foundation::Error{"storage.snapshot_hash_mismatch", "Snapshot hash must match committed project snapshot canonical hash."};
    }

    auto snapshotAppend = next.snapshots.append(*commit.snapshot);
    if (!snapshotAppend) {
      return snapshotAppend;
    }
    next.snapshotDocuments.push_back(commit.projectSnapshot);
    lastSnapshotId = commit.snapshot->id;
  } else if (next.head.has_value()) {
    lastSnapshotId = next.head->lastSnapshotId;
  }

  next.head = history::HistoryHead{
    commit.projectSnapshot.revision,
    commit.command.id,
    lastSnapshotId
  };

  state_ = std::move(next);
  return {};
}

foundation::Result<void> ProjectPackageStore::retargetPackage(ProjectPackage package) {
  if (package.projectId != state_.package.projectId) {
    return foundation::Error{"storage.package_retarget_project_id_mismatch", "Retargeted package must keep the same project id."};
  }
  if (package.schemaVersion != state_.package.schemaVersion) {
    return foundation::Error{"storage.package_retarget_schema_mismatch", "Retargeted package must keep the same schema version."};
  }
  if (package.rootPath.value.empty()) {
    return foundation::Error{"storage.package_retarget_root_empty", "Retargeted package root path must not be empty."};
  }

  state_.package = std::move(package);
  return {};
}

const ProjectPackageState& ProjectPackageStore::state() const noexcept {
  return state_;
}

} // namespace grapple::storage
