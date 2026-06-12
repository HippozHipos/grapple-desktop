#include <grapple/history/SnapshotStore.hpp>

#include <algorithm>

namespace grapple::history {

foundation::Result<void> SnapshotStore::append(SnapshotRecord record) {
  if (!record.id) {
    return foundation::Error{"history.snapshot_id_empty", "Snapshot record id must not be empty."};
  }

  const auto idExists = std::any_of(records_.begin(), records_.end(), [&](const SnapshotRecord& existing) {
    return existing.id == record.id;
  });
  if (idExists) {
    return foundation::Error{"history.snapshot_id_duplicate", "Snapshot record id already exists."};
  }

  const auto revisionExists = std::any_of(records_.begin(), records_.end(), [&](const SnapshotRecord& existing) {
    return existing.revision == record.revision;
  });
  if (revisionExists) {
    return foundation::Error{"history.snapshot_revision_duplicate", "Snapshot revision already exists."};
  }

  records_.push_back(std::move(record));
  return {};
}

const std::vector<SnapshotRecord>& SnapshotStore::records() const noexcept {
  return records_;
}

const SnapshotRecord* SnapshotStore::findByRevision(foundation::RevisionId revision) const noexcept {
  const auto iterator = std::find_if(records_.begin(), records_.end(), [&](const SnapshotRecord& record) {
    return record.revision == revision;
  });

  if (iterator == records_.end()) {
    return nullptr;
  }

  return &*iterator;
}

} // namespace grapple::history

