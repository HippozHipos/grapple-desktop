#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <optional>

namespace grapple::history {

struct HistoryHead {
  foundation::RevisionId currentRevision;
  std::optional<foundation::CommandId> lastCommandId;
  std::optional<foundation::SnapshotId> lastSnapshotId;
};

} // namespace grapple::history

