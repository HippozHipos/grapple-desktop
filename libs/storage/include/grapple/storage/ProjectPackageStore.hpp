#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/history/CommandLogStore.hpp>
#include <grapple/history/EventLogStore.hpp>
#include <grapple/history/HistoryHead.hpp>
#include <grapple/history/SnapshotStore.hpp>
#include <grapple/project/ProjectDocument.hpp>
#include <grapple/storage/ProjectPackage.hpp>

#include <optional>
#include <vector>

namespace grapple::storage {

struct AtomicProjectCommit {
  project::ProjectDocument document;
  history::CommandRecord command;
  std::vector<history::EventRecord> events;
  std::optional<history::SnapshotRecord> snapshot;
};

struct ProjectPackageState {
  ProjectPackage package;
  std::optional<project::ProjectDocument> document;
  history::CommandLogStore commandLog;
  history::EventLogStore eventLog;
  history::SnapshotStore snapshots;
  std::optional<history::HistoryHead> head;
};

class ProjectPackageStore {
public:
  explicit ProjectPackageStore(ProjectPackage package);

  foundation::Result<void> commit(const AtomicProjectCommit& commit);
  [[nodiscard]] const ProjectPackageState& state() const noexcept;

private:
  ProjectPackageState state_;
};

} // namespace grapple::storage

