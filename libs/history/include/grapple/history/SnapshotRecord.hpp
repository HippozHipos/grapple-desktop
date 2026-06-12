#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <chrono>
#include <optional>
#include <string>

namespace grapple::history {

struct SnapshotRecord {
  foundation::SnapshotId id;
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  foundation::Hash256 canonicalHash;
  foundation::FilePath documentPath;
  std::optional<std::string> label;
  std::chrono::system_clock::time_point createdAt;
};

} // namespace grapple::history

