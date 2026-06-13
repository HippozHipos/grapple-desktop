#include <grapple/graph/GraphEdge.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectCommandNames.hpp>
#include <grapple/project/ProjectEventNames.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/projection/TimelineProjector.hpp>

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
  GRAPPLE_REQUIRE(!initialSnapshot.value().settings.defaultDuration.has_value());
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
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpdateClip) == "project.update_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::DeleteClip) == "project.delete_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateCamera) == "project.create_camera");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpdateCamera) == "project.update_camera");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateEffect) == "project.create_effect");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::ConnectNodes) == "project.connect_nodes");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::DisconnectNodes) == "project.disconnect_nodes");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::SetEffectParams) == "project.set_effect_params");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::RestoreSnapshot) == "project.restore_snapshot");
  GRAPPLE_REQUIRE(project::serializedCommandSourceKind(project::CommandSourceKind::User) == "user");
  GRAPPLE_REQUIRE(project::serializedCommandSourceKind(project::CommandSourceKind::Agent) == "agent");
  GRAPPLE_REQUIRE(project::serializedCommandSourceKind(project::CommandSourceKind::Importer) == "importer");
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
  GRAPPLE_REQUIRE(serialized.find("\"settings\":{\"defaultDuration\":null}") != std::string::npos);
  GRAPPLE_REQUIRE(serialized.find("\"nodes\"") != std::string::npos);
  GRAPPLE_REQUIRE(project::hashProjectSnapshot(afterRestore.value()) == project::hashProjectSnapshot(afterRestore.value()));
  project::ProjectSnapshot durationSnapshot = afterRestore.value();
  durationSnapshot.settings.defaultDuration = foundation::TimeSeconds{12.5};
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(durationSnapshot).find("\"settings\":{\"defaultDuration\":12.5}") != std::string::npos);
  GRAPPLE_REQUIRE(!(project::hashProjectSnapshot(durationSnapshot) == project::hashProjectSnapshot(afterRestore.value())));

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
  const project::ProjectCommandEnvelope disconnectNodes{
    foundation::CommandId{"cmd_disconnect_nodes"},
    foundation::ProjectId{"proj_connection"},
    connectionSnapshot.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DisconnectNodesCommand{foundation::EdgeId{"edge_connect_nodes"}}
  };
  GRAPPLE_REQUIRE(project::commandKind(disconnectNodes.payload) == project::CommandKind::DisconnectNodes);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(disconnectNodes.payload) == "{\"edgeId\":\"edge_connect_nodes\"}");
  const auto disconnectNodesResult = connectionProject.apply(disconnectNodes);
  GRAPPLE_REQUIRE(disconnectNodesResult);
  GRAPPLE_REQUIRE(disconnectNodesResult.value().afterRevision == foundation::RevisionId{"rev_4"});
  const auto afterDisconnect = connectionProject.snapshot();
  GRAPPLE_REQUIRE(afterDisconnect);
  GRAPPLE_REQUIRE(afterDisconnect.value().graph.edges().size() == 1);
  GRAPPLE_REQUIRE(afterDisconnect.value().graph.edges()[0].id == foundation::EdgeId{"edge_connection_contains_track"});
  const auto disconnectMissing = connectionProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_disconnect_missing"},
    foundation::ProjectId{"proj_connection"},
    afterDisconnect.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DisconnectNodesCommand{foundation::EdgeId{"edge_connect_nodes"}}
  });
  GRAPPLE_REQUIRE(!disconnectMissing);
  GRAPPLE_REQUIRE(disconnectMissing.error().code == "graph.edge_missing");

  project::ProjectController clipProject{
    project::createEmptyProject(foundation::ProjectId{"proj_clip"}, "Clip Project")
  };
  const auto clipInitial = clipProject.snapshot();
  GRAPPLE_REQUIRE(clipInitial);
  const auto clipComposition = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip_composition"},
    foundation::ProjectId{"proj_clip"},
    clipInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_clip_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(clipComposition);
  const auto clipTrack = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip_track"},
    foundation::ProjectId{"proj_clip"},
    clipComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_clip_track"},
      foundation::NodeId{"node_clip_composition"},
      foundation::EdgeId{"edge_clip_contains_track"},
      "Video"
    }
  });
  GRAPPLE_REQUIRE(clipTrack);
  const timeline::ClipPayload initialClipPayload{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    1.0,
    foundation::AssetId{"asset_clip"},
    timeline::Transform{}
  };
  const auto createClip = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_clip"},
    foundation::ProjectId{"proj_clip"},
    clipTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip"},
      foundation::NodeId{"node_clip_track"},
      foundation::EdgeId{"edge_clip_contains_clip"},
      initialClipPayload
    }
  });
  GRAPPLE_REQUIRE(createClip);
  const timeline::ClipPayload updatedClipPayload{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{2.0}, foundation::TimeSeconds{12.0}},
    foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{11.0}},
    0.5,
    foundation::AssetId{"asset_clip"},
    timeline::Transform{
      foundation::Vec2{3.0, 4.0},
      foundation::Vec2{2.0, 2.0},
      0.0,
      1.0
    }
  };
  const project::ProjectCommandEnvelope updateClip{
    foundation::CommandId{"cmd_update_clip"},
    foundation::ProjectId{"proj_clip"},
    createClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateClipCommand{
      foundation::NodeId{"node_clip"},
      updatedClipPayload
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(updateClip.payload) == project::CommandKind::UpdateClip);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(updateClip.payload).find("\"nodeId\":\"node_clip\"") != std::string::npos);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(updateClip.payload).find("\"end\":12") != std::string::npos);
  const auto updateClipResult = clipProject.apply(updateClip);
  GRAPPLE_REQUIRE(updateClipResult);
  GRAPPLE_REQUIRE(updateClipResult.value().afterRevision == foundation::RevisionId{"rev_4"});
  const auto afterClipUpdate = clipProject.snapshot();
  GRAPPLE_REQUIRE(afterClipUpdate);
  const graph::GraphNode* updatedClipNode = afterClipUpdate.value().graph.findNode(foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(updatedClipNode != nullptr);
  const auto* updatedClip = std::get_if<timeline::ClipPayload>(&updatedClipNode->payload);
  GRAPPLE_REQUIRE(updatedClip != nullptr);
  GRAPPLE_REQUIRE(updatedClip->timelineRange.end == foundation::TimeSeconds{12.0});
  GRAPPLE_REQUIRE(updatedClip->playbackRate == 0.5);
  const projection::TimelineProjector clipProjector;
  const auto clipTimeline = clipProjector.buildTimelineIR(projection::BuildTimelineIRRequest{
    afterClipUpdate.value()
  });
  GRAPPLE_REQUIRE(clipTimeline);
  GRAPPLE_REQUIRE(clipTimeline.value().timeline.duration == foundation::TimeSeconds{12.0});
  const auto clipSnapshotRoundTrip = project::deserializeCanonicalProjectSnapshot(
    project::serializeCanonicalProjectSnapshot(afterClipUpdate.value())
  );
  GRAPPLE_REQUIRE(clipSnapshotRoundTrip);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalProjectSnapshot(clipSnapshotRoundTrip.value()) ==
    project::serializeCanonicalProjectSnapshot(afterClipUpdate.value())
  );
  const graph::GraphNode* roundTrippedClipNode = clipSnapshotRoundTrip.value().graph.findNode(foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(roundTrippedClipNode != nullptr);
  const auto* roundTrippedClip = std::get_if<timeline::ClipPayload>(&roundTrippedClipNode->payload);
  GRAPPLE_REQUIRE(roundTrippedClip != nullptr);
  GRAPPLE_REQUIRE(roundTrippedClip->timelineRange.end == foundation::TimeSeconds{12.0});
  GRAPPLE_REQUIRE(roundTrippedClip->sourceRange.start == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(roundTrippedClip->playbackRate == 0.5);
  GRAPPLE_REQUIRE(roundTrippedClip->transform.position.x == 3.0);
  GRAPPLE_REQUIRE(roundTrippedClip->transform.position.y == 4.0);
  const project::ProjectCommandEnvelope deleteClip{
    foundation::CommandId{"cmd_delete_clip"},
    foundation::ProjectId{"proj_clip"},
    afterClipUpdate.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteClipCommand{foundation::NodeId{"node_clip"}}
  };
  GRAPPLE_REQUIRE(project::commandKind(deleteClip.payload) == project::CommandKind::DeleteClip);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(deleteClip.payload) == "{\"nodeId\":\"node_clip\"}");
  const auto deleteClipResult = clipProject.apply(deleteClip);
  GRAPPLE_REQUIRE(deleteClipResult);
  GRAPPLE_REQUIRE(deleteClipResult.value().afterRevision == foundation::RevisionId{"rev_5"});
  const auto afterClipDelete = clipProject.snapshot();
  GRAPPLE_REQUIRE(afterClipDelete);
  GRAPPLE_REQUIRE(!afterClipDelete.value().graph.hasNode(foundation::NodeId{"node_clip"}));
  GRAPPLE_REQUIRE(afterClipDelete.value().graph.edges().size() == 1);
  const auto emptyClipTimeline = clipProjector.buildTimelineIR(projection::BuildTimelineIRRequest{
    afterClipDelete.value()
  });
  GRAPPLE_REQUIRE(emptyClipTimeline);
  GRAPPLE_REQUIRE(emptyClipTimeline.value().timeline.clips.empty());
  GRAPPLE_REQUIRE(emptyClipTimeline.value().timeline.duration == foundation::TimeSeconds{0.0});
  const auto updateMissingClip = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_update_missing_clip"},
    foundation::ProjectId{"proj_clip"},
    afterClipDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateClipCommand{
      foundation::NodeId{"node_missing_clip"},
      updatedClipPayload
    }
  });
  GRAPPLE_REQUIRE(!updateMissingClip);
  GRAPPLE_REQUIRE(updateMissingClip.error().code == "project.clip_missing");
  const auto deleteMissingClip = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_delete_missing_clip"},
    foundation::ProjectId{"proj_clip"},
    afterClipDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteClipCommand{foundation::NodeId{"node_missing_clip"}}
  });
  GRAPPLE_REQUIRE(!deleteMissingClip);
  GRAPPLE_REQUIRE(deleteMissingClip.error().code == "project.clip_missing");

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
  const auto cameraSnapshotRoundTrip = project::deserializeCanonicalProjectSnapshot(
    project::serializeCanonicalProjectSnapshot(afterCameraUpdate.value())
  );
  GRAPPLE_REQUIRE(cameraSnapshotRoundTrip);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalProjectSnapshot(cameraSnapshotRoundTrip.value()) ==
    project::serializeCanonicalProjectSnapshot(afterCameraUpdate.value())
  );
  const graph::GraphNode* roundTrippedCameraNode = cameraSnapshotRoundTrip.value().graph.findNode(foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(roundTrippedCameraNode != nullptr);
  const auto* roundTrippedCamera = std::get_if<timeline::CameraPayload>(&roundTrippedCameraNode->payload);
  GRAPPLE_REQUIRE(roundTrippedCamera != nullptr);
  GRAPPLE_REQUIRE(roundTrippedCamera->name == "Updated Camera");
  GRAPPLE_REQUIRE(roundTrippedCamera->transform.rotationDegrees == 12.0);
  GRAPPLE_REQUIRE(roundTrippedCamera->lens.focalLength == 85.0);
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

  project::ProjectController effectProject{
    project::createEmptyProject(foundation::ProjectId{"proj_effect"}, "Effect Project")
  };
  const auto effectInitial = effectProject.snapshot();
  GRAPPLE_REQUIRE(effectInitial);
  const auto effectComposition = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_composition"},
    foundation::ProjectId{"proj_effect"},
    effectInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_effect_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(effectComposition);
  const timeline::EffectPayload projectEffectPayload{
    "Effect",
    timeline::EffectImplementation{
      timeline::EffectImplementationKind::Python,
      "prepare",
      timeline::EffectSource{
        timeline::EffectSourceKind::InlineSource,
        "python",
        "def prepare(ctx):\n  return {}\n",
        std::nullopt,
        foundation::stableHash("def prepare(ctx):\n  return {}\n")
      }
    },
    timeline::EffectPortSet{{timeline::EffectPort{"input"}}, {timeline::EffectPort{"output"}}},
    timeline::ParamSet{
      {timeline::Param{
        "smoothing",
        0.25,
        timeline::Param::Control{
          "Smoothing",
          timeline::Param::NumericControl{0.0, 1.0, 0.01}
        }
      }}
    },
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
  };
  const auto invalidEffectTarget = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_invalid_effect_target"},
    foundation::ProjectId{"proj_effect"},
    effectComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_invalid_effect"},
      foundation::NodeId{"node_effect_composition"},
      foundation::EdgeId{"edge_invalid_effect_target"},
      projectEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(!invalidEffectTarget);
  GRAPPLE_REQUIRE(invalidEffectTarget.error().code == "project.effect_target_invalid");
  const auto afterInvalidEffectTarget = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterInvalidEffectTarget);
  GRAPPLE_REQUIRE(afterInvalidEffectTarget.value().revision == effectComposition.value().afterRevision);
  GRAPPLE_REQUIRE(!afterInvalidEffectTarget.value().graph.hasNode(foundation::NodeId{"node_invalid_effect"}));
  const auto effectTrack = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect_track"},
    foundation::ProjectId{"proj_effect"},
    effectComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_effect_track"},
      foundation::NodeId{"node_effect_composition"},
      foundation::EdgeId{"edge_effect_contains_track"},
      "Video"
    }
  });
  GRAPPLE_REQUIRE(effectTrack);
  timeline::EffectPayload parameterlessEffectPayload = projectEffectPayload;
  parameterlessEffectPayload.params = timeline::ParamSet{};
  const auto blackBoxAgentEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_black_box_agent_effect"},
    foundation::ProjectId{"proj_effect"},
    effectTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_black_box_agent_effect"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_black_box_agent_effect_target"},
      parameterlessEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(!blackBoxAgentEffect);
  GRAPPLE_REQUIRE(blackBoxAgentEffect.error().code == "project.agent_effect_params_missing");
  const auto afterBlackBoxAgentEffect = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterBlackBoxAgentEffect);
  GRAPPLE_REQUIRE(afterBlackBoxAgentEffect.value().revision == effectTrack.value().afterRevision);
  GRAPPLE_REQUIRE(!afterBlackBoxAgentEffect.value().graph.hasNode(foundation::NodeId{"node_black_box_agent_effect"}));
  timeline::EffectPayload unlabeledEffectPayload = projectEffectPayload;
  unlabeledEffectPayload.params.values[0].control.label = "";
  const auto unlabeledAgentEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_unlabeled_agent_effect"},
    foundation::ProjectId{"proj_effect"},
    effectTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_unlabeled_agent_effect"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_unlabeled_agent_effect_target"},
      unlabeledEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(!unlabeledAgentEffect);
  GRAPPLE_REQUIRE(unlabeledAgentEffect.error().code == "project.agent_effect_param_label_missing");
  timeline::EffectPayload uncontrolledEffectPayload = projectEffectPayload;
  uncontrolledEffectPayload.params.values[0].control.numeric = std::nullopt;
  const auto uncontrolledAgentEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_uncontrolled_agent_effect"},
    foundation::ProjectId{"proj_effect"},
    effectTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_uncontrolled_agent_effect"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_uncontrolled_agent_effect_target"},
      uncontrolledEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(!uncontrolledAgentEffect);
  GRAPPLE_REQUIRE(uncontrolledAgentEffect.error().code == "project.agent_effect_param_control_missing");
  const auto userParameterlessEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_user_parameterless_effect"},
    foundation::ProjectId{"proj_effect"},
    effectTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_user_parameterless_effect"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_user_parameterless_effect_target"},
      parameterlessEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(userParameterlessEffect);
  const auto validTrackEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_valid_track_effect"},
    foundation::ProjectId{"proj_effect"},
    userParameterlessEffect.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_track_effect"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_track_effect_target"},
      projectEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(validTrackEffect);
  const auto afterTrackEffect = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterTrackEffect);
  GRAPPLE_REQUIRE(afterTrackEffect.value().graph.hasNode(foundation::NodeId{"node_track_effect"}));
  const auto effectSnapshotRoundTrip = project::deserializeCanonicalProjectSnapshot(
    project::serializeCanonicalProjectSnapshot(afterTrackEffect.value())
  );
  GRAPPLE_REQUIRE(effectSnapshotRoundTrip);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalProjectSnapshot(effectSnapshotRoundTrip.value()) ==
    project::serializeCanonicalProjectSnapshot(afterTrackEffect.value())
  );
  const graph::GraphNode* roundTrippedEffectNode = effectSnapshotRoundTrip.value().graph.findNode(foundation::NodeId{"node_track_effect"});
  GRAPPLE_REQUIRE(roundTrippedEffectNode != nullptr);
  const auto* roundTrippedEffect = std::get_if<timeline::EffectPayload>(&roundTrippedEffectNode->payload);
  GRAPPLE_REQUIRE(roundTrippedEffect != nullptr);
  GRAPPLE_REQUIRE(roundTrippedEffect->implementation.kind == timeline::EffectImplementationKind::Python);
  GRAPPLE_REQUIRE(roundTrippedEffect->implementation.source.inlineSource == "def prepare(ctx):\n  return {}\n");
  GRAPPLE_REQUIRE(roundTrippedEffect->ports.inputs.size() == 1);
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values.size() == 1);
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].name == "smoothing");
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].control.label == "Smoothing");
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].control.numeric.has_value());
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].control.numeric->min == 0.0);
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].control.numeric->max == 1.0);
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].control.numeric->step == 0.01);
  GRAPPLE_REQUIRE(roundTrippedEffect->activeRange.end == foundation::TimeSeconds{1.0});

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
