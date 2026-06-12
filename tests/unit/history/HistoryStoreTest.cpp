#include <grapple/history/CommandLogStore.hpp>
#include <grapple/history/EventLogStore.hpp>
#include <grapple/history/HistorySerializer.hpp>
#include <grapple/history/SnapshotStore.hpp>

#include <TestAssert.hpp>

#include <chrono>

int main() {
  using namespace grapple;
  const auto now = std::chrono::system_clock::now();

  history::CommandLogStore commandLog;
  const auto commandAppend = commandLog.append(history::CommandRecord{
    foundation::CommandId{"cmd_1"},
    foundation::ProjectId{"proj_history"},
    foundation::RevisionId{"rev_0"},
    foundation::RevisionId{"rev_1"},
    "project.create_composition",
    R"({"nodeId":"node_1"})",
    "agent",
    foundation::RunId{"run_1"},
    "test-agent",
    now
  });
  GRAPPLE_REQUIRE(commandAppend);
  GRAPPLE_REQUIRE(commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(commandLog.records()[0].sourceKind == "agent");
  GRAPPLE_REQUIRE(commandLog.records()[0].sourceRunId == foundation::RunId{"run_1"});
  GRAPPLE_REQUIRE(commandLog.records()[0].sourceActorName == "test-agent");
  const std::string serializedCommandLog = history::serializeCanonicalCommandLog(commandLog);
  GRAPPLE_REQUIRE(serializedCommandLog.find("\"sourceRunId\":\"run_1\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedCommandLog.find("\"createdAtMs\":") != std::string::npos);
  const auto parsedCommandLog = history::deserializeCanonicalCommandLog(serializedCommandLog);
  GRAPPLE_REQUIRE(parsedCommandLog);
  GRAPPLE_REQUIRE(history::serializeCanonicalCommandLog(parsedCommandLog.value()) == serializedCommandLog);

  const auto duplicateCommand = commandLog.append(commandLog.records().front());
  GRAPPLE_REQUIRE(!duplicateCommand);
  GRAPPLE_REQUIRE(duplicateCommand.error().code == "history.command_id_duplicate");

  history::EventLogStore eventLog;
  const auto eventAppend = eventLog.append(history::EventRecord{
    foundation::EventId{"event_1"},
    foundation::ProjectId{"proj_history"},
    foundation::RevisionId{"rev_1"},
    "project.command_applied",
    R"({"commandId":"cmd_1"})",
    now
  });
  GRAPPLE_REQUIRE(eventAppend);
  GRAPPLE_REQUIRE(eventLog.records().size() == 1);
  const std::string serializedEventLog = history::serializeCanonicalEventLog(eventLog);
  GRAPPLE_REQUIRE(serializedEventLog.find("\"serializedName\":\"project.command_applied\"") != std::string::npos);
  const auto parsedEventLog = history::deserializeCanonicalEventLog(serializedEventLog);
  GRAPPLE_REQUIRE(parsedEventLog);
  GRAPPLE_REQUIRE(history::serializeCanonicalEventLog(parsedEventLog.value()) == serializedEventLog);

  history::SnapshotStore snapshots;
  const auto snapshotAppend = snapshots.append(history::SnapshotRecord{
    foundation::SnapshotId{"snap_1"},
    foundation::ProjectId{"proj_history"},
    foundation::RevisionId{"rev_1"},
    foundation::stableHash("snapshot"),
    foundation::FilePath{"snapshots/rev_1.json"},
    std::optional<std::string>{"initial"},
    now
  });
  GRAPPLE_REQUIRE(snapshotAppend);
  GRAPPLE_REQUIRE(snapshots.records().size() == 1);
  GRAPPLE_REQUIRE(snapshots.findByRevision(foundation::RevisionId{"rev_1"}) != nullptr);
  GRAPPLE_REQUIRE(snapshots.findByRevision(foundation::RevisionId{"rev_missing"}) == nullptr);

  const auto duplicateSnapshot = snapshots.append(history::SnapshotRecord{
    foundation::SnapshotId{"snap_2"},
    foundation::ProjectId{"proj_history"},
    foundation::RevisionId{"rev_1"},
    foundation::stableHash("snapshot"),
    foundation::FilePath{"snapshots/rev_1_again.json"},
    std::nullopt,
    now
  });
  GRAPPLE_REQUIRE(!duplicateSnapshot);
  GRAPPLE_REQUIRE(duplicateSnapshot.error().code == "history.snapshot_revision_duplicate");

  return 0;
}
