#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectCommandNames.hpp>
#include <grapple/project/ProjectEventNames.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/history/HistorySerializer.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>
#include <grapple/storage/ProjectPackageStore.hpp>
#include <grapple/storage/ProjectPackageWriter.hpp>
#include <grapple/storage/SchemaMigration.hpp>

#include <TestAssert.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

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
    "user",
    std::nullopt,
    "test",
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
  GRAPPLE_REQUIRE(store.state().commandLog.records()[0].sourceKind == "user");
  GRAPPLE_REQUIRE(!store.state().commandLog.records()[0].sourceRunId.has_value());
  GRAPPLE_REQUIRE(store.state().commandLog.records()[0].sourceActorName == "test");
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 2);
  GRAPPLE_REQUIRE(store.state().eventLog.records()[0].id == foundation::EventId{"event_cmd_1_0"});
  GRAPPLE_REQUIRE(store.state().eventLog.records()[0].serializedName == "project.command_applied");
  GRAPPLE_REQUIRE(store.state().eventLog.records()[1].id == foundation::EventId{"event_cmd_1_1"});
  GRAPPLE_REQUIRE(store.state().eventLog.records()[1].serializedName == "project.changed");
  GRAPPLE_REQUIRE(store.state().snapshots.records().size() == 1);
  GRAPPLE_REQUIRE(store.state().snapshotDocuments.size() == 1);
  GRAPPLE_REQUIRE(store.state().snapshotDocuments[0].revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(store.state().snapshotDocuments[0]) == project::serializeCanonicalProjectSnapshot(committedSnapshot.value()));
  GRAPPLE_REQUIRE(store.state().head.has_value());
  GRAPPLE_REQUIRE(store.state().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(store.state().head->lastCommandId == foundation::CommandId{"cmd_1"});
  GRAPPLE_REQUIRE(store.state().head->lastSnapshotId == foundation::SnapshotId{"snap_1"});

  const auto manifest = storage::buildProjectPackageManifest(store.state());
  GRAPPLE_REQUIRE(manifest);
  GRAPPLE_REQUIRE(manifest.value().projectId == foundation::ProjectId{"proj_storage"});
  GRAPPLE_REQUIRE(manifest.value().schemaVersion == 1);
  GRAPPLE_REQUIRE(manifest.value().schemaMigrationLogPath == foundation::FilePath{"history/schema_migrations.json"});
  GRAPPLE_REQUIRE(manifest.value().head.has_value());
  GRAPPLE_REQUIRE(manifest.value().head->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(manifest.value().head->lastCommandId == foundation::CommandId{"cmd_1"});
  GRAPPLE_REQUIRE(manifest.value().head->lastSnapshotId == foundation::SnapshotId{"snap_1"});
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot.has_value());
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot->id == foundation::SnapshotId{"snap_1"});
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot->canonicalHash == committedSnapshot.value().canonicalHash);
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot->documentPath == foundation::FilePath{"snapshots/rev_1.json"});
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot->label == std::optional<std::string>{"first"});
  GRAPPLE_REQUIRE(storage::serializeCanonicalProjectPackageManifest(manifest.value()) ==
    "{\"schemaVersion\":1,\"projectId\":\"proj_storage\",\"commandLogPath\":\"history/commands.json\",\"eventLogPath\":\"history/events.json\",\"schemaMigrationLogPath\":\"history/schema_migrations.json\",\"head\":{\"revision\":\"rev_1\",\"lastCommandId\":\"cmd_1\",\"lastSnapshotId\":\"snap_1\"},\"latestSnapshot\":{\"id\":\"snap_1\",\"revision\":\"rev_1\",\"canonicalHash\":\"" +
    committedSnapshot.value().canonicalHash.toHex() +
    "\",\"documentPath\":\"snapshots/rev_1.json\",\"label\":\"first\"}}");

  storage::ProjectPackageStore emptyStore{storage::ProjectPackage{
    foundation::ProjectId{"proj_storage"},
    foundation::FilePath{"project.grapple"},
    1
  }};
  const auto emptyManifest = storage::buildProjectPackageManifest(emptyStore.state());
  GRAPPLE_REQUIRE(emptyManifest);
  GRAPPLE_REQUIRE(storage::serializeCanonicalProjectPackageManifest(emptyManifest.value()) ==
    "{\"schemaVersion\":1,\"projectId\":\"proj_storage\",\"commandLogPath\":\"history/commands.json\",\"eventLogPath\":\"history/events.json\",\"schemaMigrationLogPath\":\"history/schema_migrations.json\",\"head\":null,\"latestSnapshot\":null}");

  const std::filesystem::path packageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_storage_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const storage::ProjectPackageWriter packageWriter;
  const storage::ProjectPackage diskPackage{
    foundation::ProjectId{"proj_storage"},
    foundation::FilePath{packageRoot.string()},
    1
  };
  const auto writtenManifestPath = packageWriter.writeManifest(manifest.value(), diskPackage);
  GRAPPLE_REQUIRE(writtenManifestPath);
  GRAPPLE_REQUIRE(writtenManifestPath.value().value == (packageRoot / "manifest.json").lexically_normal().string());
  std::ifstream manifestFile{writtenManifestPath.value().value, std::ios::binary};
  GRAPPLE_REQUIRE(manifestFile.good());
  std::ostringstream manifestContents;
  manifestContents << manifestFile.rdbuf();
  GRAPPLE_REQUIRE(manifestContents.str() == storage::serializeCanonicalProjectPackageManifest(manifest.value()));
  const auto parsedManifest = storage::deserializeCanonicalProjectPackageManifest(manifestContents.str());
  GRAPPLE_REQUIRE(parsedManifest);
  GRAPPLE_REQUIRE(storage::serializeCanonicalProjectPackageManifest(parsedManifest.value()) == manifestContents.str());
  const auto manifestWithExtraRootField = storage::deserializeCanonicalProjectPackageManifest(
    "{\"schemaVersion\":1,\"projectId\":\"proj_storage\",\"commandLogPath\":\"history/commands.json\",\"eventLogPath\":\"history/events.json\",\"schemaMigrationLogPath\":\"history/schema_migrations.json\",\"head\":null,\"latestSnapshot\":null,\"metadata\":{}}"
  );
  GRAPPLE_REQUIRE(!manifestWithExtraRootField);
  GRAPPLE_REQUIRE(manifestWithExtraRootField.error().code == "storage.manifest_json_invalid");
  GRAPPLE_REQUIRE(manifestWithExtraRootField.error().message.find("Unexpected serialized field") != std::string::npos);
  const auto manifestWithEmptyProjectId = storage::deserializeCanonicalProjectPackageManifest(
    "{\"schemaVersion\":1,\"projectId\":\"\",\"commandLogPath\":\"history/commands.json\",\"eventLogPath\":\"history/events.json\",\"schemaMigrationLogPath\":\"history/schema_migrations.json\",\"head\":null,\"latestSnapshot\":null}"
  );
  GRAPPLE_REQUIRE(!manifestWithEmptyProjectId);
  GRAPPLE_REQUIRE(manifestWithEmptyProjectId.error().code == "storage.manifest_json_invalid");
  GRAPPLE_REQUIRE(manifestWithEmptyProjectId.error().message.find("non-empty") != std::string::npos);
  const auto manifestWithExtraHeadField = storage::deserializeCanonicalProjectPackageManifest(
    "{\"schemaVersion\":1,\"projectId\":\"proj_storage\",\"commandLogPath\":\"history/commands.json\",\"eventLogPath\":\"history/events.json\",\"schemaMigrationLogPath\":\"history/schema_migrations.json\",\"head\":{\"revision\":\"rev_1\",\"lastCommandId\":\"cmd_1\",\"lastSnapshotId\":\"snap_1\",\"metadata\":{}},\"latestSnapshot\":null}"
  );
  GRAPPLE_REQUIRE(!manifestWithExtraHeadField);
  GRAPPLE_REQUIRE(manifestWithExtraHeadField.error().code == "storage.manifest_json_invalid");
  GRAPPLE_REQUIRE(manifestWithExtraHeadField.error().message.find("Unexpected serialized field") != std::string::npos);
  const auto manifestWithEmptyHeadCommandId = storage::deserializeCanonicalProjectPackageManifest(
    "{\"schemaVersion\":1,\"projectId\":\"proj_storage\",\"commandLogPath\":\"history/commands.json\",\"eventLogPath\":\"history/events.json\",\"schemaMigrationLogPath\":\"history/schema_migrations.json\",\"head\":{\"revision\":\"rev_1\",\"lastCommandId\":\"\",\"lastSnapshotId\":\"snap_1\"},\"latestSnapshot\":null}"
  );
  GRAPPLE_REQUIRE(!manifestWithEmptyHeadCommandId);
  GRAPPLE_REQUIRE(manifestWithEmptyHeadCommandId.error().code == "storage.manifest_json_invalid");
  GRAPPLE_REQUIRE(manifestWithEmptyHeadCommandId.error().message.find("non-empty") != std::string::npos);
  const auto manifestWithExtraSnapshotField = storage::deserializeCanonicalProjectPackageManifest(
    std::string{
      "{\"schemaVersion\":1,\"projectId\":\"proj_storage\",\"commandLogPath\":\"history/commands.json\",\"eventLogPath\":\"history/events.json\",\"schemaMigrationLogPath\":\"history/schema_migrations.json\",\"head\":null,\"latestSnapshot\":{\"id\":\"snap_1\",\"revision\":\"rev_1\",\"canonicalHash\":\""
    } +
      committedSnapshot.value().canonicalHash.toHex() +
      "\",\"documentPath\":\"snapshots/rev_1.json\",\"label\":null,\"metadata\":{}}}"
  );
  GRAPPLE_REQUIRE(!manifestWithExtraSnapshotField);
  GRAPPLE_REQUIRE(manifestWithExtraSnapshotField.error().code == "storage.manifest_json_invalid");
  GRAPPLE_REQUIRE(manifestWithExtraSnapshotField.error().message.find("Unexpected serialized field") != std::string::npos);

  const auto writtenSnapshotPath = packageWriter.writeSnapshot(storage::ProjectSnapshotWriteRequest{
    diskPackage,
    committedSnapshot.value(),
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_1"},
      foundation::FilePath{"snapshots/rev_1.json"},
      std::optional<std::string>{"first"}
    }
  });
  GRAPPLE_REQUIRE(writtenSnapshotPath);
  GRAPPLE_REQUIRE(writtenSnapshotPath.value().value == (packageRoot / "snapshots/rev_1.json").lexically_normal().string());
  std::ifstream snapshotFile{writtenSnapshotPath.value().value, std::ios::binary};
  GRAPPLE_REQUIRE(snapshotFile.good());
  std::ostringstream snapshotContents;
  snapshotContents << snapshotFile.rdbuf();
  GRAPPLE_REQUIRE(snapshotContents.str() == project::serializeCanonicalProjectSnapshot(committedSnapshot.value()));
  const auto writtenCommandLogPath = packageWriter.writeCommandLog(storage::ProjectCommandLogWriteRequest{
    diskPackage,
    manifest.value().commandLogPath,
    store.state().commandLog
  });
  GRAPPLE_REQUIRE(writtenCommandLogPath);
  GRAPPLE_REQUIRE(writtenCommandLogPath.value().value == (packageRoot / "history/commands.json").lexically_normal().string());
  std::ifstream commandLogFile{writtenCommandLogPath.value().value, std::ios::binary};
  GRAPPLE_REQUIRE(commandLogFile.good());
  std::ostringstream commandLogContents;
  commandLogContents << commandLogFile.rdbuf();
  GRAPPLE_REQUIRE(commandLogContents.str() == history::serializeCanonicalCommandLog(store.state().commandLog));
  const auto writtenEventLogPath = packageWriter.writeEventLog(storage::ProjectEventLogWriteRequest{
    diskPackage,
    manifest.value().eventLogPath,
    store.state().eventLog
  });
  GRAPPLE_REQUIRE(writtenEventLogPath);
  GRAPPLE_REQUIRE(writtenEventLogPath.value().value == (packageRoot / "history/events.json").lexically_normal().string());
  std::ifstream eventLogFile{writtenEventLogPath.value().value, std::ios::binary};
  GRAPPLE_REQUIRE(eventLogFile.good());
  std::ostringstream eventLogContents;
  eventLogContents << eventLogFile.rdbuf();
  GRAPPLE_REQUIRE(eventLogContents.str() == history::serializeCanonicalEventLog(store.state().eventLog));
  const storage::SchemaMigrationLog emptyDiskMigrationLog;
  const auto writtenSchemaMigrationLogPath = packageWriter.writeSchemaMigrationLog(storage::ProjectSchemaMigrationLogWriteRequest{
    diskPackage,
    manifest.value().schemaMigrationLogPath,
    emptyDiskMigrationLog
  });
  GRAPPLE_REQUIRE(writtenSchemaMigrationLogPath);
  GRAPPLE_REQUIRE(writtenSchemaMigrationLogPath.value().value == (packageRoot / "history/schema_migrations.json").lexically_normal().string());
  std::ifstream schemaMigrationLogFile{writtenSchemaMigrationLogPath.value().value, std::ios::binary};
  GRAPPLE_REQUIRE(schemaMigrationLogFile.good());
  std::ostringstream schemaMigrationLogContents;
  schemaMigrationLogContents << schemaMigrationLogFile.rdbuf();
  GRAPPLE_REQUIRE(schemaMigrationLogContents.str() == "[]");
  const storage::ProjectPackageReader packageReader;
  const auto readPackage = packageReader.readPackage(foundation::FilePath{packageRoot.string()});
  GRAPPLE_REQUIRE(readPackage);
  GRAPPLE_REQUIRE(readPackage.value().projectId == foundation::ProjectId{"proj_storage"});
  GRAPPLE_REQUIRE(readPackage.value().rootPath == foundation::FilePath{packageRoot.string()});
  GRAPPLE_REQUIRE(readPackage.value().schemaVersion == 1);
  const auto readManifestAtRoot = packageReader.readManifestAtRoot(foundation::FilePath{packageRoot.string()});
  GRAPPLE_REQUIRE(readManifestAtRoot);
  GRAPPLE_REQUIRE(storage::serializeCanonicalProjectPackageManifest(readManifestAtRoot.value()) == manifestContents.str());
  const auto readManifest = packageReader.readManifest(diskPackage);
  GRAPPLE_REQUIRE(readManifest);
  GRAPPLE_REQUIRE(storage::serializeCanonicalProjectPackageManifest(readManifest.value()) == manifestContents.str());
  const auto loadedLatestSnapshot = packageReader.readLatestSnapshot(diskPackage);
  GRAPPLE_REQUIRE(loadedLatestSnapshot);
  GRAPPLE_REQUIRE(storage::serializeCanonicalProjectPackageManifest(loadedLatestSnapshot.value().manifest) == manifestContents.str());
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(loadedLatestSnapshot.value().snapshot) == snapshotContents.str());
  const auto loadedHistoryLogs = packageReader.readHistoryLogs(diskPackage);
  GRAPPLE_REQUIRE(loadedHistoryLogs);
  GRAPPLE_REQUIRE(history::serializeCanonicalCommandLog(loadedHistoryLogs.value().commandLog) == commandLogContents.str());
  GRAPPLE_REQUIRE(history::serializeCanonicalEventLog(loadedHistoryLogs.value().eventLog) == eventLogContents.str());
  auto openedSession = storage::ProjectPackageSession::open(diskPackage);
  GRAPPLE_REQUIRE(openedSession);
  const auto openedSnapshot = openedSession.value().snapshot();
  GRAPPLE_REQUIRE(openedSnapshot);
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(openedSnapshot.value()) == snapshotContents.str());
  GRAPPLE_REQUIRE(openedSession.value().packageState().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(openedSession.value().packageState().eventLog.records().size() == 2);
  GRAPPLE_REQUIRE(openedSession.value().packageState().snapshotDocuments.size() == 1);
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(openedSession.value().packageState().snapshotDocuments[0]) == snapshotContents.str());
  GRAPPLE_REQUIRE(openedSession.value().packageState().head.has_value());
  GRAPPLE_REQUIRE(openedSession.value().packageState().head->currentRevision == foundation::RevisionId{"rev_1"});
  const auto openedTrack = openedSession.value().applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_opened_track"},
      foundation::ProjectId{"proj_storage"},
      openedSnapshot.value().revision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "opened"},
      project::CreateTrackCommand{
        foundation::NodeId{"node_opened_track"},
        foundation::NodeId{"node_composition"},
        foundation::EdgeId{"edge_opened_contains_track"},
        "Opened Track"
      }
    },
    storage::ProjectCommitRecordOptions{std::chrono::system_clock::now(), std::nullopt}
  );
  GRAPPLE_REQUIRE(openedTrack);
  GRAPPLE_REQUIRE(openedTrack.value().snapshot.revision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(openedSession.value().packageState().commandLog.records().size() == 2);

  project::ProjectSnapshot invalidSnapshot = committedSnapshot.value();
  GRAPPLE_REQUIRE(invalidSnapshot.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_invalid_track"},
    graph::NodeKind::Track,
    timeline::TrackPayload{"Invalid Track"},
    true
  }));
  GRAPPLE_REQUIRE(invalidSnapshot.graph.addNode(graph::GraphNode{
    foundation::NodeId{"node_invalid_clip"},
    graph::NodeKind::Clip,
    timeline::ClipPayload{
      timeline::ClipKind::Video,
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      1.0,
      foundation::AssetId{"asset_missing_clip"},
      timeline::Transform{}
    },
    true
  }));
  GRAPPLE_REQUIRE(invalidSnapshot.graph.addEdge(graph::GraphEdge{
    foundation::EdgeId{"edge_invalid_track_contains_clip"},
    graph::EdgeKind::Contains,
    foundation::NodeId{"node_invalid_track"},
    graph::PortName{},
    foundation::NodeId{"node_invalid_clip"},
    graph::PortName{},
    0,
    true
  }));
  invalidSnapshot.canonicalHash = project::hashProjectSnapshot(invalidSnapshot);
  storage::ProjectPackageManifest invalidManifest = manifest.value();
  invalidManifest.latestSnapshot->canonicalHash = invalidSnapshot.canonicalHash;
  const auto writtenInvalidSnapshot = packageWriter.writeSnapshot(storage::ProjectSnapshotWriteRequest{
    diskPackage,
    invalidSnapshot,
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_1"},
      foundation::FilePath{"snapshots/rev_1.json"},
      std::optional<std::string>{"first"}
    }
  });
  GRAPPLE_REQUIRE(writtenInvalidSnapshot);
  const auto writtenInvalidManifest = packageWriter.writeManifest(invalidManifest, diskPackage);
  GRAPPLE_REQUIRE(writtenInvalidManifest);
  const auto invalidOpen = storage::ProjectPackageSession::open(diskPackage);
  GRAPPLE_REQUIRE(!invalidOpen);
  GRAPPLE_REQUIRE(invalidOpen.error().code == "project.snapshot_clip_asset_missing");

  const auto restoredSnapshot = packageWriter.writeSnapshot(storage::ProjectSnapshotWriteRequest{
    diskPackage,
    committedSnapshot.value(),
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_1"},
      foundation::FilePath{"snapshots/rev_1.json"},
      std::optional<std::string>{"first"}
    }
  });
  GRAPPLE_REQUIRE(restoredSnapshot);
  const auto restoredManifest = packageWriter.writeManifest(manifest.value(), diskPackage);
  GRAPPLE_REQUIRE(restoredManifest);

  storage::ProjectPackageManifest wrongProjectManifest = manifest.value();
  wrongProjectManifest.projectId = foundation::ProjectId{"proj_other"};
  const auto wrongProjectManifestPath = packageWriter.writeManifest(wrongProjectManifest, diskPackage);
  GRAPPLE_REQUIRE(!wrongProjectManifestPath);
  GRAPPLE_REQUIRE(wrongProjectManifestPath.error().code == "storage.manifest_project_id_mismatch");

  storage::ProjectPackageManifest wrongSchemaManifest = manifest.value();
  wrongSchemaManifest.schemaVersion = 2;
  const auto wrongSchemaManifestPath = packageWriter.writeManifest(wrongSchemaManifest, diskPackage);
  GRAPPLE_REQUIRE(!wrongSchemaManifestPath);
  GRAPPLE_REQUIRE(wrongSchemaManifestPath.error().code == "storage.manifest_schema_mismatch");

  const auto absoluteSnapshotPath = packageWriter.writeSnapshot(storage::ProjectSnapshotWriteRequest{
    storage::ProjectPackage{
      foundation::ProjectId{"proj_storage"},
      foundation::FilePath{packageRoot.string()},
      1
    },
    committedSnapshot.value(),
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_absolute"},
      foundation::FilePath{(packageRoot / "bad.json").string()},
      std::nullopt
    }
  });
  GRAPPLE_REQUIRE(!absoluteSnapshotPath);
  GRAPPLE_REQUIRE(absoluteSnapshotPath.error().code == "storage.snapshot_document_path_absolute");
  const auto wrongProjectSnapshotPath = packageWriter.writeSnapshot(storage::ProjectSnapshotWriteRequest{
    storage::ProjectPackage{
      foundation::ProjectId{"proj_other"},
      foundation::FilePath{packageRoot.string()},
      1
    },
    committedSnapshot.value(),
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_wrong_project"},
      foundation::FilePath{"snapshots/wrong_project.json"},
      std::nullopt
    }
  });
  GRAPPLE_REQUIRE(!wrongProjectSnapshotPath);
  GRAPPLE_REQUIRE(wrongProjectSnapshotPath.error().code == "storage.snapshot_project_id_mismatch");
  std::filesystem::remove_all(packageRoot);

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

  storage::ProjectPackageState missingHeadSnapshotState = store.state();
  missingHeadSnapshotState.head->lastSnapshotId = foundation::SnapshotId{"snap_missing"};
  const auto missingHeadSnapshotManifest = storage::buildProjectPackageManifest(missingHeadSnapshotState);
  GRAPPLE_REQUIRE(!missingHeadSnapshotManifest);
  GRAPPLE_REQUIRE(missingHeadSnapshotManifest.error().code == "storage.package_head_snapshot_missing");

  storage::SchemaMigrationLog migrationLog;
  const auto migrationAppliedAt = std::chrono::system_clock::time_point{std::chrono::milliseconds{123456}};
  const auto appendedMigration = migrationLog.append(storage::SchemaMigrationRecord{
    "storage.add_schema_migration_log",
    1,
    2,
    migrationAppliedAt
  });
  GRAPPLE_REQUIRE(appendedMigration);
  GRAPPLE_REQUIRE(migrationLog.records().size() == 1);
  GRAPPLE_REQUIRE(migrationLog.records()[0].operationName == "storage.add_schema_migration_log");
  GRAPPLE_REQUIRE(migrationLog.records()[0].fromSchemaVersion == 1);
  GRAPPLE_REQUIRE(migrationLog.records()[0].toSchemaVersion == 2);
  const std::string serializedMigrationLog = storage::serializeCanonicalSchemaMigrationLog(migrationLog);
  GRAPPLE_REQUIRE(serializedMigrationLog == "[{\"operationName\":\"storage.add_schema_migration_log\",\"fromSchemaVersion\":1,\"toSchemaVersion\":2,\"appliedAtMs\":123456}]");
  const auto parsedMigrationLog = storage::deserializeCanonicalSchemaMigrationLog(serializedMigrationLog);
  GRAPPLE_REQUIRE(parsedMigrationLog);
  GRAPPLE_REQUIRE(storage::serializeCanonicalSchemaMigrationLog(parsedMigrationLog.value()) == serializedMigrationLog);

  storage::SchemaMigrationLog invalidMigrationLog;
  const auto unnamedMigration = invalidMigrationLog.append(storage::SchemaMigrationRecord{
    "",
    1,
    2,
    migrationAppliedAt
  });
  GRAPPLE_REQUIRE(!unnamedMigration);
  GRAPPLE_REQUIRE(unnamedMigration.error().code == "storage.schema_migration_operation_empty");
  const auto unchangedMigration = invalidMigrationLog.append(storage::SchemaMigrationRecord{
    "storage.no_change",
    2,
    2,
    migrationAppliedAt
  });
  GRAPPLE_REQUIRE(!unchangedMigration);
  GRAPPLE_REQUIRE(unchangedMigration.error().code == "storage.schema_migration_version_unchanged");
  const auto invalidParsedMigrationLog = storage::deserializeCanonicalSchemaMigrationLog(R"([{"operationName":"","fromSchemaVersion":1,"toSchemaVersion":2,"appliedAtMs":123456}])");
  GRAPPLE_REQUIRE(!invalidParsedMigrationLog);
  GRAPPLE_REQUIRE(invalidParsedMigrationLog.error().code == "storage.schema_migration_json_invalid");
  const auto migrationLogWithExtraField = storage::deserializeCanonicalSchemaMigrationLog(R"([{"operationName":"storage.extra","fromSchemaVersion":1,"toSchemaVersion":2,"appliedAtMs":123456,"extra":true}])");
  GRAPPLE_REQUIRE(!migrationLogWithExtraField);
  GRAPPLE_REQUIRE(migrationLogWithExtraField.error().code == "storage.schema_migration_json_invalid");
  GRAPPLE_REQUIRE(migrationLogWithExtraField.error().message.find("Unexpected serialized field") != std::string::npos);

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
