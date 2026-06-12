#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectCommandNames.hpp>
#include <grapple/project/ProjectEventNames.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/storage/ProjectPackageStore.hpp>

#include <TestAssert.hpp>

#include <chrono>

namespace {

grapple::history::CommandRecord makeCommandRecord(
  grapple::foundation::CommandId commandId,
  grapple::foundation::RevisionId beforeRevision,
  grapple::foundation::RevisionId afterRevision
) {
  return grapple::history::CommandRecord{
    std::move(commandId),
    grapple::foundation::ProjectId{"proj_storage"},
    std::move(beforeRevision),
    std::move(afterRevision),
    std::string{grapple::project::serializedCommandName(grapple::project::CommandKind::CreateComposition)},
    grapple::project::serializeCanonicalCommandPayload(grapple::project::CreateCompositionCommand{
      grapple::foundation::NodeId{"node_composition"},
      "Main"
    }),
    std::chrono::system_clock::now()
  };
}

grapple::history::EventRecord makeEventRecord(
  grapple::foundation::EventId eventId,
  grapple::foundation::RevisionId revision
) {
  const grapple::foundation::RevisionId eventRevision = revision;
  return grapple::history::EventRecord{
    std::move(eventId),
    grapple::foundation::ProjectId{"proj_storage"},
    std::move(revision),
    std::string{grapple::project::serializedEventName(grapple::project::EventKind::ProjectCommandApplied)},
    grapple::project::serializeCanonicalEventPayload(grapple::project::ProjectCommandAppliedEvent{
      grapple::foundation::CommandId{"cmd_1"},
      grapple::foundation::RevisionId{"rev_0"},
      eventRevision
    }),
    std::chrono::system_clock::now()
  };
}

} // namespace

int main() {
  using namespace grapple;

  storage::ProjectPackageStore store{storage::ProjectPackage{
    foundation::ProjectId{"proj_storage"},
    foundation::FilePath{"project.grapple"},
    1
  }};

  project::ProjectController controller{
    project::createEmptyProject(foundation::ProjectId{"proj_storage"}, "Storage Project")
  };

  const auto initialSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(initialSnapshot);

  const auto commandResult = controller.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_1"},
    foundation::ProjectId{"proj_storage"},
    initialSnapshot.value().document.revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(commandResult);

  const auto committedSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(committedSnapshot);

  const auto commit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value().document,
    makeCommandRecord(
      foundation::CommandId{"cmd_1"},
      commandResult.value().beforeRevision,
      commandResult.value().afterRevision
    ),
    {makeEventRecord(foundation::EventId{"event_1"}, commandResult.value().afterRevision)},
    history::SnapshotRecord{
      foundation::SnapshotId{"snap_1"},
      foundation::ProjectId{"proj_storage"},
      commandResult.value().afterRevision,
      project::hashProjectSnapshot(committedSnapshot.value()),
      foundation::FilePath{"snapshots/rev_1.json"},
      std::optional<std::string>{"first"},
      std::chrono::system_clock::now()
    }
  });
  GRAPPLE_REQUIRE(commit);

  GRAPPLE_REQUIRE(store.state().document.has_value());
  GRAPPLE_REQUIRE(store.state().document->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().snapshots.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().head.has_value());
  GRAPPLE_REQUIRE(store.state().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(store.state().head->lastCommandId == foundation::CommandId{"cmd_1"});
  GRAPPLE_REQUIRE(store.state().head->lastSnapshotId == foundation::SnapshotId{"snap_1"});

  const auto duplicateCommit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value().document,
    makeCommandRecord(
      foundation::CommandId{"cmd_1"},
      commandResult.value().beforeRevision,
      commandResult.value().afterRevision
    ),
    {makeEventRecord(foundation::EventId{"event_2"}, commandResult.value().afterRevision)},
    std::nullopt
  });
  GRAPPLE_REQUIRE(!duplicateCommit);
  GRAPPLE_REQUIRE(duplicateCommit.error().code == "history.command_id_duplicate");
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 1);

  const auto badHashCommit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value().document,
    makeCommandRecord(
      foundation::CommandId{"cmd_2"},
      commandResult.value().beforeRevision,
      commandResult.value().afterRevision
    ),
    {makeEventRecord(foundation::EventId{"event_3"}, commandResult.value().afterRevision)},
    history::SnapshotRecord{
      foundation::SnapshotId{"snap_bad_hash"},
      foundation::ProjectId{"proj_storage"},
      commandResult.value().afterRevision,
      foundation::stableHash("not_the_document"),
      foundation::FilePath{"snapshots/bad.json"},
      std::nullopt,
      std::chrono::system_clock::now()
    }
  });
  GRAPPLE_REQUIRE(!badHashCommit);
  GRAPPLE_REQUIRE(badHashCommit.error().code == "storage.snapshot_hash_mismatch");
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().snapshots.records().size() == 1);

  return 0;
}
