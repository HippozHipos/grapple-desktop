#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectCommandNames.hpp>
#include <grapple/project/ProjectEventNames.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>
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

  const project::ProjectCommandEnvelope createCompositionCommand{
    foundation::CommandId{"cmd_1"},
    foundation::ProjectId{"proj_storage"},
    initialSnapshot.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  };
  const auto commandResult = controller.apply(createCompositionCommand);
  GRAPPLE_REQUIRE(commandResult);

  const auto committedSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(committedSnapshot);

  const auto commit = store.commit(storage::makeAtomicProjectCommit(
    committedSnapshot.value(),
    createCompositionCommand,
    commandResult.value(),
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      storage::SnapshotCommitRecord{
        foundation::SnapshotId{"snap_1"},
        foundation::FilePath{"snapshots/rev_1.json"},
        std::optional<std::string>{"first"}
      }
    }
  ));
  GRAPPLE_REQUIRE(commit);

  GRAPPLE_REQUIRE(store.state().projectSnapshot.has_value());
  GRAPPLE_REQUIRE(store.state().projectSnapshot->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().commandLog.records()[0].serializedName == "project.create_composition");
  GRAPPLE_REQUIRE(store.state().commandLog.records()[0].serializedPayload == "{\"nodeId\":\"node_composition\",\"name\":\"Main\"}");
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 2);
  GRAPPLE_REQUIRE(store.state().eventLog.records()[0].id == foundation::EventId{"event_cmd_1_0"});
  GRAPPLE_REQUIRE(store.state().eventLog.records()[0].serializedName == "project.command_applied");
  GRAPPLE_REQUIRE(store.state().eventLog.records()[1].id == foundation::EventId{"event_cmd_1_1"});
  GRAPPLE_REQUIRE(store.state().eventLog.records()[1].serializedName == "project.changed");
  GRAPPLE_REQUIRE(store.state().snapshots.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().head.has_value());
  GRAPPLE_REQUIRE(store.state().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(store.state().head->lastCommandId == foundation::CommandId{"cmd_1"});
  GRAPPLE_REQUIRE(store.state().head->lastSnapshotId == foundation::SnapshotId{"snap_1"});

  const auto duplicateCommit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value(),
    makeCommandRecord(
      foundation::CommandId{"cmd_1"},
      commandResult.value().afterRevision,
      commandResult.value().afterRevision
    ),
    {makeEventRecord(foundation::EventId{"event_2"}, commandResult.value().afterRevision)},
    std::nullopt
  });
  GRAPPLE_REQUIRE(!duplicateCommit);
  GRAPPLE_REQUIRE(duplicateCommit.error().code == "history.command_id_duplicate");
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 2);

  const auto badCommandRevisionCommit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value(),
    makeCommandRecord(
      foundation::CommandId{"cmd_bad_command_revision"},
      commandResult.value().afterRevision,
      foundation::RevisionId{"rev_wrong"}
    ),
    {makeEventRecord(foundation::EventId{"event_bad_command_revision"}, commandResult.value().afterRevision)},
    std::nullopt
  });
  GRAPPLE_REQUIRE(!badCommandRevisionCommit);
  GRAPPLE_REQUIRE(badCommandRevisionCommit.error().code == "storage.command_revision_mismatch");
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 2);

  const auto badEventRevisionCommit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value(),
    makeCommandRecord(
      foundation::CommandId{"cmd_bad_event_revision"},
      commandResult.value().afterRevision,
      commandResult.value().afterRevision
    ),
    {makeEventRecord(foundation::EventId{"event_bad_revision"}, foundation::RevisionId{"rev_wrong"})},
    std::nullopt
  });
  GRAPPLE_REQUIRE(!badEventRevisionCommit);
  GRAPPLE_REQUIRE(badEventRevisionCommit.error().code == "storage.event_revision_mismatch");
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 2);

  const auto staleBeforeRevisionCommit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value(),
    makeCommandRecord(
      foundation::CommandId{"cmd_stale_before_revision"},
      foundation::RevisionId{"rev_0"},
      commandResult.value().afterRevision
    ),
    {makeEventRecord(foundation::EventId{"event_stale_before_revision"}, commandResult.value().afterRevision)},
    std::nullopt
  });
  GRAPPLE_REQUIRE(!staleBeforeRevisionCommit);
  GRAPPLE_REQUIRE(staleBeforeRevisionCommit.error().code == "storage.command_before_revision_mismatch");
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 2);

  const auto badHashCommit = store.commit(storage::AtomicProjectCommit{
    committedSnapshot.value(),
    makeCommandRecord(
      foundation::CommandId{"cmd_2"},
      commandResult.value().afterRevision,
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
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 2);
  GRAPPLE_REQUIRE(store.state().snapshots.records().size() == 1);

  storage::ProjectPackageSession session{
    project::createEmptyProject(foundation::ProjectId{"proj_session"}, "Session Project"),
    storage::ProjectPackage{
      foundation::ProjectId{"proj_session"},
      foundation::FilePath{"session.grapple"},
      1
    }
  };
  const auto sessionInitial = session.snapshot();
  GRAPPLE_REQUIRE(sessionInitial);
  const auto sessionComposition = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_session_1"},
      foundation::ProjectId{"proj_session"},
      sessionInitial.value().revision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
      project::CreateCompositionCommand{foundation::NodeId{"node_session_composition"}, "Main"}
    },
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      std::nullopt
    }
  );
  GRAPPLE_REQUIRE(sessionComposition);
  GRAPPLE_REQUIRE(sessionComposition.value().snapshot.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().head.has_value());
  GRAPPLE_REQUIRE(session.packageState().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == 1);

  const auto duplicateSessionCommand = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_session_1"},
      foundation::ProjectId{"proj_session"},
      sessionComposition.value().snapshot.revision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
      project::CreateTrackCommand{
        foundation::NodeId{"node_session_track"},
        foundation::NodeId{"node_session_composition"},
        foundation::EdgeId{"edge_session_contains_track"},
        "Video"
      }
    },
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      std::nullopt
    }
  );
  GRAPPLE_REQUIRE(!duplicateSessionCommand);
  GRAPPLE_REQUIRE(duplicateSessionCommand.error().code == "history.command_id_duplicate");
  const auto afterDuplicateSessionCommand = session.snapshot();
  GRAPPLE_REQUIRE(afterDuplicateSessionCommand);
  GRAPPLE_REQUIRE(afterDuplicateSessionCommand.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterDuplicateSessionCommand.value().graph.nodes().size() == 1);
  GRAPPLE_REQUIRE(session.packageState().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == 1);

  return 0;
}
