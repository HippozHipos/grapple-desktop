#include <grapple/graph/GraphEdge.hpp>
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
  GRAPPLE_REQUIRE(initialSnapshot.value().revision == foundation::RevisionId{"rev_0"});
  GRAPPLE_REQUIRE(initialSnapshot.value().canonicalHash == project::hashProjectSnapshot(initialSnapshot.value()));
  GRAPPLE_REQUIRE(initialSnapshot.value().graph.nodes().empty());

  const auto createComposition = controller.apply(
    makeCreateComposition(initialSnapshot.value().revision)
  );
  GRAPPLE_REQUIRE(createComposition);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(makeCreateComposition(initialSnapshot.value().revision).payload) == "{\"nodeId\":\"node_composition\",\"name\":\"Main\"}");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::RegisterAsset) == "project.register_asset");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateComposition) == "project.create_composition");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateTrack) == "project.create_track");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateClip) == "project.create_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateCamera) == "project.create_camera");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpdateCamera) == "project.update_camera");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateEffect) == "project.create_effect");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::ConnectNodes) == "project.connect_nodes");
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
  GRAPPLE_REQUIRE(afterComposition.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterComposition.value().graph.nodes().size() == 1);

  const auto staleCommand = controller.apply(
    makeCreateComposition(foundation::RevisionId{"rev_0"})
  );
  GRAPPLE_REQUIRE(!staleCommand);
  GRAPPLE_REQUIRE(staleCommand.error().code == "project.expected_revision_mismatch");

  const auto afterStale = controller.snapshot();
  GRAPPLE_REQUIRE(afterStale);
  GRAPPLE_REQUIRE(afterStale.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterStale.value().graph.nodes().size() == 1);

  const project::ProjectCommandEnvelope createTrack{
    foundation::CommandId{"cmd_create_track"},
    foundation::ProjectId{"proj_test"},
    afterStale.value().revision,
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
  GRAPPLE_REQUIRE(finalSnapshot.value().graph.nodes().size() == 2);
  GRAPPLE_REQUIRE(finalSnapshot.value().graph.edges().size() == 1);

  const project::ProjectCommandEnvelope restoreCompositionSnapshot{
    foundation::CommandId{"cmd_restore_snapshot"},
    foundation::ProjectId{"proj_test"},
    finalSnapshot.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::RestoreSnapshotCommand{
      foundation::SnapshotId{"snap_after_composition"},
      afterComposition.value()
    }
  };
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(restoreCompositionSnapshot.payload).find("\"snapshotId\":\"snap_after_composition\"") != std::string::npos);

  const auto restoreResult = controller.apply(restoreCompositionSnapshot);
  GRAPPLE_REQUIRE(restoreResult);
  GRAPPLE_REQUIRE(restoreResult.value().beforeRevision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(restoreResult.value().afterRevision == foundation::RevisionId{"rev_3"});

  const auto afterRestore = controller.snapshot();
  GRAPPLE_REQUIRE(afterRestore);
  GRAPPLE_REQUIRE(afterRestore.value().revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(afterRestore.value().revisionNumber == 3);
  GRAPPLE_REQUIRE(afterRestore.value().graph.nodes().size() == 1);
  GRAPPLE_REQUIRE(afterRestore.value().graph.edges().empty());

  const std::string serialized = project::serializeCanonicalProjectSnapshot(afterRestore.value());
  GRAPPLE_REQUIRE(serialized.find("\"projectId\":\"proj_test\"") != std::string::npos);
  GRAPPLE_REQUIRE(serialized.find("\"revision\":\"rev_3\"") != std::string::npos);
  GRAPPLE_REQUIRE(serialized.find("\"nodes\"") != std::string::npos);
  GRAPPLE_REQUIRE(project::hashProjectSnapshot(afterRestore.value()) == project::hashProjectSnapshot(afterRestore.value()));

  project::ProjectController assetProject{
    project::createEmptyProject(foundation::ProjectId{"proj_assets"}, "Asset Project")
  };
  const auto assetInitialSnapshot = assetProject.snapshot();
  GRAPPLE_REQUIRE(assetInitialSnapshot);
  GRAPPLE_REQUIRE(assetInitialSnapshot.value().assets.assets().empty());

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
    assetInitialSnapshot.value().revision,
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
  GRAPPLE_REQUIRE(assetSnapshot.value().assets.assets().size() == 1);
  GRAPPLE_REQUIRE(assetSnapshot.value().assets.find(foundation::AssetId{"asset_walking_woman"}) != nullptr);
  const std::string serializedAssetProject = project::serializeCanonicalProjectSnapshot(assetSnapshot.value());
  GRAPPLE_REQUIRE(serializedAssetProject.find("\"assets\":[{\"id\":\"asset_walking_woman\"") != std::string::npos);

  const auto duplicateAsset = assetProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_register_asset_duplicate"},
    foundation::ProjectId{"proj_assets"},
    assetSnapshot.value().revision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "import"},
    project::RegisterAssetCommand{registeredAsset}
  });
  GRAPPLE_REQUIRE(!duplicateAsset);
  GRAPPLE_REQUIRE(duplicateAsset.error().code == "asset.id_duplicate");
  const auto afterDuplicateAsset = assetProject.snapshot();
  GRAPPLE_REQUIRE(afterDuplicateAsset);
  GRAPPLE_REQUIRE(afterDuplicateAsset.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterDuplicateAsset.value().assets.assets().size() == 1);

  project::ProjectController connectionProject{
    project::createEmptyProject(foundation::ProjectId{"proj_connection"}, "Connection Project")
  };
  const auto connectionInitialSnapshot = connectionProject.snapshot();
  GRAPPLE_REQUIRE(connectionInitialSnapshot);
  const auto connectionComposition = connectionProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_connection_composition"},
    foundation::ProjectId{"proj_connection"},
    connectionInitialSnapshot.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_connection_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(connectionComposition);
  const auto connectionTrack = connectionProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_connection_track"},
    foundation::ProjectId{"proj_connection"},
    connectionComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_connection_track"},
      foundation::NodeId{"node_connection_composition"},
      foundation::EdgeId{"edge_connection_contains_track"},
      "Video"
    }
  });
  GRAPPLE_REQUIRE(connectionTrack);

  const project::ProjectCommandEnvelope connectNodes{
    foundation::CommandId{"cmd_connect_nodes"},
    foundation::ProjectId{"proj_connection"},
    connectionTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::ConnectNodesCommand{
      foundation::EdgeId{"edge_connect_nodes"},
      foundation::NodeId{"node_connection_composition"},
      graph::PortName{"output"},
      foundation::NodeId{"node_connection_track"},
      graph::PortName{"input"},
      2
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(connectNodes.payload) == project::CommandKind::ConnectNodes);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(connectNodes.payload) ==
    "{\"edgeId\":\"edge_connect_nodes\",\"sourceNodeId\":\"node_connection_composition\",\"sourcePort\":\"output\",\"targetNodeId\":\"node_connection_track\",\"targetPort\":\"input\",\"order\":2}"
  );
  const auto connectNodesResult = connectionProject.apply(connectNodes);
  GRAPPLE_REQUIRE(connectNodesResult);
  GRAPPLE_REQUIRE(connectNodesResult.value().afterRevision == foundation::RevisionId{"rev_3"});
  const auto connectionSnapshot = connectionProject.snapshot();
  GRAPPLE_REQUIRE(connectionSnapshot);
  GRAPPLE_REQUIRE(connectionSnapshot.value().graph.edges().size() == 2);

  project::ProjectController cameraProject{
    project::createEmptyProject(foundation::ProjectId{"proj_camera"}, "Camera Project")
  };
  const auto cameraInitial = cameraProject.snapshot();
  GRAPPLE_REQUIRE(cameraInitial);
  const auto cameraComposition = cameraProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_camera_composition"},
    foundation::ProjectId{"proj_camera"},
    cameraInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_camera_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(cameraComposition);
  const auto createCamera = cameraProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_camera"},
    foundation::ProjectId{"proj_camera"},
    cameraComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCameraCommand{
      foundation::NodeId{"node_camera"},
      foundation::NodeId{"node_camera_composition"},
      foundation::EdgeId{"edge_camera_contains_camera"},
      timeline::CameraPayload{
        "Camera",
        timeline::Transform{},
        timeline::CameraLens{35.0}
      }
    }
  });
  GRAPPLE_REQUIRE(createCamera);
  const project::ProjectCommandEnvelope updateCamera{
    foundation::CommandId{"cmd_update_camera"},
    foundation::ProjectId{"proj_camera"},
    createCamera.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateCameraCommand{
      foundation::NodeId{"node_camera"},
      timeline::CameraPayload{
        "Updated Camera",
        timeline::Transform{
          foundation::Vec2{1.0, 2.0},
          foundation::Vec2{1.5, 1.5},
          12.0,
          0.8
        },
        timeline::CameraLens{85.0}
      }
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(updateCamera.payload) == project::CommandKind::UpdateCamera);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(updateCamera.payload).find("\"nodeId\":\"node_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(updateCamera.payload).find("\"focalLength\":85") != std::string::npos);
  const auto updateCameraResult = cameraProject.apply(updateCamera);
  GRAPPLE_REQUIRE(updateCameraResult);
  GRAPPLE_REQUIRE(updateCameraResult.value().afterRevision == foundation::RevisionId{"rev_3"});
  const auto afterCameraUpdate = cameraProject.snapshot();
  GRAPPLE_REQUIRE(afterCameraUpdate);
  const graph::GraphNode* updatedCameraNode = afterCameraUpdate.value().graph.findNode(foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(updatedCameraNode != nullptr);
  const auto* updatedCameraPayload = std::get_if<timeline::CameraPayload>(&updatedCameraNode->payload);
  GRAPPLE_REQUIRE(updatedCameraPayload != nullptr);
  GRAPPLE_REQUIRE(updatedCameraPayload->name == "Updated Camera");
  GRAPPLE_REQUIRE(updatedCameraPayload->lens.focalLength == 85.0);
  const auto updateMissingCamera = cameraProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_update_missing_camera"},
    foundation::ProjectId{"proj_camera"},
    afterCameraUpdate.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateCameraCommand{
      foundation::NodeId{"node_missing_camera"},
      *updatedCameraPayload
    }
  });
  GRAPPLE_REQUIRE(!updateMissingCamera);
  GRAPPLE_REQUIRE(updateMissingCamera.error().code == "project.camera_missing");

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
  GRAPPLE_REQUIRE(snapshotResult->snapshot.revision == foundation::RevisionId{"rev_3"});

  return 0;
}
