#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectCommandNames.hpp>
#include <grapple/project/ProjectEventNames.hpp>
#include <grapple/project/ProjectSerializer.hpp>

#include <TestAssert.hpp>

namespace {

grapple::project::ProjectCommandEnvelope makeCreateComposition(
  grapple::foundation::RevisionId expectedRevision
) {
  return grapple::project::ProjectCommandEnvelope{
    grapple::foundation::CommandId{"cmd_create_composition"},
    grapple::foundation::ProjectId{"proj_test"},
    std::move(expectedRevision),
    grapple::project::CommandSource{grapple::project::CommandSourceKind::User, std::nullopt, "test"},
    grapple::project::CreateCompositionCommand{
      grapple::foundation::NodeId{"node_composition"},
      "Main"
    }
  };
}

} // namespace

int main() {
  using namespace grapple;

  project::ProjectController controller{
    project::createEmptyProject(foundation::ProjectId{"proj_test"}, "Test Project")
  };

  const auto initialSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(initialSnapshot);
  GRAPPLE_REQUIRE(initialSnapshot.value().document.revision == foundation::RevisionId{"rev_0"});
  GRAPPLE_REQUIRE(initialSnapshot.value().document.graph.nodes().empty());

  const auto createComposition = controller.apply(
    makeCreateComposition(initialSnapshot.value().document.revision)
  );
  GRAPPLE_REQUIRE(createComposition);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(makeCreateComposition(initialSnapshot.value().document.revision).payload) == "{\"nodeId\":\"node_composition\",\"name\":\"Main\"}");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::RegisterAsset) == "project.register_asset");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateComposition) == "project.create_composition");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateTrack) == "project.create_track");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateClip) == "project.create_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateCamera) == "project.create_camera");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateEffect) == "project.create_effect");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::SetEffectParams) == "project.set_effect_params");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::RestoreSnapshot) == "project.restore_snapshot");
  GRAPPLE_REQUIRE(project::serializedEventName(project::EventKind::ProjectCommandApplied) == "project.command_applied");
  GRAPPLE_REQUIRE(project::serializedEventName(project::EventKind::ProjectChanged) == "project.changed");
  GRAPPLE_REQUIRE(createComposition.value().beforeRevision == foundation::RevisionId{"rev_0"});
  GRAPPLE_REQUIRE(createComposition.value().afterRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(createComposition.value().events.size() == 2);
  GRAPPLE_REQUIRE(project::eventKind(createComposition.value().events[0]) == project::EventKind::ProjectCommandApplied);
  GRAPPLE_REQUIRE(project::eventKind(createComposition.value().events[1]) == project::EventKind::ProjectChanged);
  GRAPPLE_REQUIRE(project::serializeCanonicalEventPayload(createComposition.value().events[0]) == "{\"commandId\":\"cmd_create_composition\",\"beforeRevision\":\"rev_0\",\"afterRevision\":\"rev_1\"}");

  const auto afterComposition = controller.snapshot();
  GRAPPLE_REQUIRE(afterComposition);
  GRAPPLE_REQUIRE(afterComposition.value().document.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterComposition.value().document.graph.nodes().size() == 1);

  const auto staleCommand = controller.apply(
    makeCreateComposition(foundation::RevisionId{"rev_0"})
  );
  GRAPPLE_REQUIRE(!staleCommand);
  GRAPPLE_REQUIRE(staleCommand.error().code == "project.expected_revision_mismatch");

  const auto afterStale = controller.snapshot();
  GRAPPLE_REQUIRE(afterStale);
  GRAPPLE_REQUIRE(afterStale.value().document.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterStale.value().document.graph.nodes().size() == 1);

  const project::ProjectCommandEnvelope createTrack{
    foundation::CommandId{"cmd_create_track"},
    foundation::ProjectId{"proj_test"},
    afterStale.value().document.revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_track"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_track"},
      "Video"
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(createTrack.payload) == project::CommandKind::CreateTrack);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(createTrack.payload).find("\"containmentEdgeId\":\"edge_contains_track\"") != std::string::npos);

  const auto trackResult = controller.apply(createTrack);
  GRAPPLE_REQUIRE(trackResult);
  GRAPPLE_REQUIRE(trackResult.value().afterRevision == foundation::RevisionId{"rev_2"});

  const auto finalSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(finalSnapshot);
  GRAPPLE_REQUIRE(finalSnapshot.value().document.graph.nodes().size() == 2);
  GRAPPLE_REQUIRE(finalSnapshot.value().document.graph.edges().size() == 1);

  const project::ProjectCommandEnvelope restoreCompositionSnapshot{
    foundation::CommandId{"cmd_restore_snapshot"},
    foundation::ProjectId{"proj_test"},
    finalSnapshot.value().document.revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::RestoreSnapshotCommand{
      foundation::SnapshotId{"snap_after_composition"},
      afterComposition.value().document
    }
  };
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(restoreCompositionSnapshot.payload).find("\"snapshotId\":\"snap_after_composition\"") != std::string::npos);

  const auto restoreResult = controller.apply(restoreCompositionSnapshot);
  GRAPPLE_REQUIRE(restoreResult);
  GRAPPLE_REQUIRE(restoreResult.value().beforeRevision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(restoreResult.value().afterRevision == foundation::RevisionId{"rev_3"});

  const auto afterRestore = controller.snapshot();
  GRAPPLE_REQUIRE(afterRestore);
  GRAPPLE_REQUIRE(afterRestore.value().document.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(afterRestore.value().document.revisionNumber == 3);
  GRAPPLE_REQUIRE(afterRestore.value().document.graph.nodes().size() == 1);
  GRAPPLE_REQUIRE(afterRestore.value().document.graph.edges().empty());

  const std::string serialized = project::serializeCanonicalProjectDocument(afterRestore.value().document);
  GRAPPLE_REQUIRE(serialized.find("\"projectId\":\"proj_test\"") != std::string::npos);
  GRAPPLE_REQUIRE(serialized.find("\"revision\":\"rev_3\"") != std::string::npos);
  GRAPPLE_REQUIRE(serialized.find("\"nodes\"") != std::string::npos);
  GRAPPLE_REQUIRE(project::hashProjectSnapshot(afterRestore.value()) == project::hashProjectSnapshot(afterRestore.value()));

  project::ProjectController assetProject{
    project::createEmptyProject(foundation::ProjectId{"proj_assets"}, "Asset Project")
  };
  const auto assetInitialSnapshot = assetProject.snapshot();
  GRAPPLE_REQUIRE(assetInitialSnapshot);
  GRAPPLE_REQUIRE(assetInitialSnapshot.value().document.assets.assets().empty());

  const asset::Asset registeredAsset{
    foundation::AssetId{"asset_walking_woman"},
    "Walking Woman",
    asset::AssetMetadata{
      asset::AssetMediaType::Video,
      foundation::FilePath{"/media/walking-woman.mp4"},
      foundation::FilePath{"/media/walking-woman.jpg"},
      foundation::TimeSeconds{10.0},
      foundation::Resolution{1080, 1920},
      foundation::FrameRate{30, 1}
    }
  };
  const project::ProjectCommandEnvelope registerAsset{
    foundation::CommandId{"cmd_register_asset"},
    foundation::ProjectId{"proj_assets"},
    assetInitialSnapshot.value().document.revision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "import"},
    project::RegisterAssetCommand{registeredAsset}
  };
  GRAPPLE_REQUIRE(project::commandKind(registerAsset.payload) == project::CommandKind::RegisterAsset);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(registerAsset.payload).find("\"asset\":{\"id\":\"asset_walking_woman\"") != std::string::npos);

  const auto registerAssetResult = assetProject.apply(registerAsset);
  GRAPPLE_REQUIRE(registerAssetResult);
  GRAPPLE_REQUIRE(registerAssetResult.value().afterRevision == foundation::RevisionId{"rev_1"});

  const auto assetSnapshot = assetProject.snapshot();
  GRAPPLE_REQUIRE(assetSnapshot);
  GRAPPLE_REQUIRE(assetSnapshot.value().document.assets.assets().size() == 1);
  GRAPPLE_REQUIRE(assetSnapshot.value().document.assets.find(foundation::AssetId{"asset_walking_woman"}) != nullptr);
  const std::string serializedAssetProject = project::serializeCanonicalProjectDocument(assetSnapshot.value().document);
  GRAPPLE_REQUIRE(serializedAssetProject.find("\"assets\":[{\"id\":\"asset_walking_woman\"") != std::string::npos);

  const auto duplicateAsset = assetProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_register_asset_duplicate"},
    foundation::ProjectId{"proj_assets"},
    assetSnapshot.value().document.revision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "import"},
    project::RegisterAssetCommand{registeredAsset}
  });
  GRAPPLE_REQUIRE(!duplicateAsset);
  GRAPPLE_REQUIRE(duplicateAsset.error().code == "asset.id_duplicate");
  const auto afterDuplicateAsset = assetProject.snapshot();
  GRAPPLE_REQUIRE(afterDuplicateAsset);
  GRAPPLE_REQUIRE(afterDuplicateAsset.value().document.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterDuplicateAsset.value().document.assets.assets().size() == 1);

  const auto graphQuery = controller.query(project::GetGraphQuery{});
  GRAPPLE_REQUIRE(graphQuery);
  const auto* graphResult = std::get_if<project::GraphResult>(&graphQuery.value());
  GRAPPLE_REQUIRE(graphResult != nullptr);
  GRAPPLE_REQUIRE(graphResult->graph.nodes().size() == 1);
  GRAPPLE_REQUIRE(graphResult->graph.edges().empty());

  const auto snapshotQuery = controller.query(project::GetProjectSnapshotQuery{});
  GRAPPLE_REQUIRE(snapshotQuery);
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&snapshotQuery.value());
  GRAPPLE_REQUIRE(snapshotResult != nullptr);
  GRAPPLE_REQUIRE(snapshotResult->snapshot.document.revision == foundation::RevisionId{"rev_3"});

  return 0;
}
