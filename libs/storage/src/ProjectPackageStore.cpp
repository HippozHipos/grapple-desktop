#include <grapple/storage/ProjectPackageStore.hpp>

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

