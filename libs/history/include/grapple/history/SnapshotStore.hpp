#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/history/SnapshotRecord.hpp>

#include <optional>
#include <vector>

namespace grapple::history {

class SnapshotStore {
public:
  foundation::Result<void> append(SnapshotRecord record);
  [[nodiscard]] const std::vector<SnapshotRecord>& records() const noexcept;
  [[nodiscard]] const SnapshotRecord* findByRevision(foundation::RevisionId revision) const noexcept;

private:
  std::vector<SnapshotRecord> records_;
};

} // namespace grapple::history

