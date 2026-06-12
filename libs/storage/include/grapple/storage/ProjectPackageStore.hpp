#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/history/CommandLogStore.hpp>
#include <grapple/history/EventLogStore.hpp>
#include <grapple/history/HistoryHead.hpp>
#include <grapple/history/SnapshotStore.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/storage/ProjectPackage.hpp>

#include <optional>
#include <vector>

namespace grapple::storage {

struct AtomicProjectCommit {
  project::ProjectSnapshot projectSnapshot;
  history::CommandRecord command;
  std::vector<history::EventRecord> events;
  std::optional<history::SnapshotRecord> snapshot;
};

struct ProjectPackageState {
  ProjectPackage package;
  std::optional<project::ProjectSnapshot> projectSnapshot;
  history::CommandLogStore commandLog;
  history::EventLogStore eventLog;
  history::SnapshotStore snapshots;
  std::optional<history::HistoryHead> head;
};

class ProjectPackageStore {
public:
  explicit ProjectPackageStore(ProjectPackage package);
  explicit ProjectPackageStore(ProjectPackageState state);

  foundation::Result<void> commit(const AtomicProjectCommit& commit);
  [[nodiscard]] const ProjectPackageState& state() const noexcept;

private:
  ProjectPackageState state_;
};

} // namespace grapple::storage
