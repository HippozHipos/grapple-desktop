#include <grapple/graph/GraphEdge.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectCommandNames.hpp>
#include <grapple/project/ProjectEventNames.hpp>
#include <grapple/project/ProjectMediaPlacement.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/projection/TimelineProjector.hpp>

#include <TestAssert.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

bool allUnique(std::vector<std::string_view> values) {
  std::sort(values.begin(), values.end());
  return std::adjacent_find(values.begin(), values.end()) == values.end();
}

class TestIdAllocator final : public grapple::project::IProjectIdAllocator {
public:
  grapple::foundation::CommandId nextCommandId() override {
    return grapple::foundation::CommandId{"cmd_" + std::to_string(++commandId_)};
  }

  grapple::foundation::AssetId nextAssetId(const std::string& stem) override {
    return grapple::foundation::AssetId{stem + "_" + std::to_string(++assetId_)};
  }

  grapple::foundation::NodeId nextNodeId(const std::string& stem) override {
    return grapple::foundation::NodeId{stem + "_" + std::to_string(++nodeId_)};
  }

  grapple::foundation::EdgeId nextEdgeId(const std::string& stem) override {
    return grapple::foundation::EdgeId{stem + "_" + std::to_string(++edgeId_)};
  }

  grapple::foundation::KeyframeId nextKeyframeId(const std::string& stem) override {
    return grapple::foundation::KeyframeId{stem + "_" + std::to_string(++keyframeId_)};
  }

private:
  int commandId_ = 0;
  int assetId_ = 0;
  int nodeId_ = 0;
  int edgeId_ = 0;
  int keyframeId_ = 0;
};

grapple::asset::Asset makeVideoAsset(
  grapple::foundation::AssetId assetId,
  std::string name
) {
  return grapple::asset::Asset{
    std::move(assetId),
    std::move(name),
    grapple::asset::AssetMetadata{
      grapple::asset::AssetMediaType::Video,
      grapple::foundation::FilePath{"/media/test.mp4"},
      std::nullopt,
      grapple::foundation::TimeSeconds{10.0},
      grapple::foundation::Resolution{1920, 1080},
      grapple::foundation::FrameRate{30, 1}
    }
  };
}

grapple::asset::Asset makeDurationlessAsset(
  grapple::foundation::AssetId assetId,
  std::string name,
  grapple::asset::AssetMediaType mediaType,
  std::string sourcePath
) {
  return grapple::asset::Asset{
    std::move(assetId),
    std::move(name),
    grapple::asset::AssetMetadata{
      mediaType,
      grapple::foundation::FilePath{std::move(sourcePath)},
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt
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
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::DeleteTrack) == "project.delete_track");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::AddMediaToTimeline) == "project.add_media_to_timeline");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateClip) == "project.create_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::MoveClip) == "project.move_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::TrimClip) == "project.trim_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpdateClip) == "project.update_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::DeleteClip) == "project.delete_clip");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateCamera) == "project.create_camera");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpdateCamera) == "project.update_camera");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateEffect) == "project.create_effect");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::DeleteEffect) == "project.delete_effect");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::ConnectPorts) == "project.connect_ports");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::DisconnectPorts) == "project.disconnect_ports");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpdateEffectParamValue) == "project.update_effect_param_value");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpsertEffectParamKeyframe) == "project.upsert_effect_param_keyframe");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::DeleteEffectParamKeyframe) == "project.delete_effect_param_keyframe");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::CreateNote) == "project.create_note");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::UpdateNote) == "project.update_note");
  GRAPPLE_REQUIRE(project::serializedCommandName(project::CommandKind::RestoreSnapshot) == "project.restore_snapshot");
  GRAPPLE_REQUIRE(project::serializedCommandSourceKind(project::CommandSourceKind::User) == "user");
  GRAPPLE_REQUIRE(project::serializedCommandSourceKind(project::CommandSourceKind::Agent) == "agent");
  GRAPPLE_REQUIRE(project::serializedCommandSourceKind(project::CommandSourceKind::Importer) == "importer");
  GRAPPLE_REQUIRE(project::serializedCommandSourceKind(project::CommandSourceKind::Migration) == "migration");
  GRAPPLE_REQUIRE(project::serializedEventName(project::EventKind::ProjectCommandApplied) == "project.command_applied");
  GRAPPLE_REQUIRE(project::serializedEventName(project::EventKind::ProjectChanged) == "project.changed");
  GRAPPLE_REQUIRE(allUnique({
    project::serializedCommandName(project::CommandKind::RegisterAsset),
    project::serializedCommandName(project::CommandKind::CreateComposition),
    project::serializedCommandName(project::CommandKind::CreateTrack),
    project::serializedCommandName(project::CommandKind::DeleteTrack),
    project::serializedCommandName(project::CommandKind::AddMediaToTimeline),
    project::serializedCommandName(project::CommandKind::CreateClip),
    project::serializedCommandName(project::CommandKind::MoveClip),
    project::serializedCommandName(project::CommandKind::TrimClip),
    project::serializedCommandName(project::CommandKind::UpdateClip),
    project::serializedCommandName(project::CommandKind::DeleteClip),
    project::serializedCommandName(project::CommandKind::CreateCamera),
    project::serializedCommandName(project::CommandKind::UpdateCamera),
    project::serializedCommandName(project::CommandKind::CreateEffect),
    project::serializedCommandName(project::CommandKind::DeleteEffect),
    project::serializedCommandName(project::CommandKind::ConnectPorts),
    project::serializedCommandName(project::CommandKind::DisconnectPorts),
    project::serializedCommandName(project::CommandKind::UpdateEffectParamValue),
    project::serializedCommandName(project::CommandKind::UpsertEffectParamKeyframe),
    project::serializedCommandName(project::CommandKind::DeleteEffectParamKeyframe),
    project::serializedCommandName(project::CommandKind::CreateNote),
    project::serializedCommandName(project::CommandKind::UpdateNote),
    project::serializedCommandName(project::CommandKind::RestoreSnapshot)
  }));

  TestIdAllocator placementIds;
  const auto imagePlacement = project::buildMediaPlacementDraft(
    placementIds,
    makeDurationlessAsset(
      foundation::AssetId{"asset_image"},
      "Still",
      asset::AssetMediaType::Image,
      "/media/still.png"
    ),
    std::nullopt,
    std::nullopt,
    {}
  );
  GRAPPLE_REQUIRE(imagePlacement);
  GRAPPLE_REQUIRE(imagePlacement.value().command.clip.payload.kind == timeline::ClipKind::Image);
  GRAPPLE_REQUIRE(imagePlacement.value().command.clip.payload.timelineRange.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(imagePlacement.value().command.clip.payload.timelineRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(imagePlacement.value().command.clip.payload.sourceRange.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(imagePlacement.value().command.clip.payload.sourceRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(imagePlacement.value().command.camera.has_value());

  TestIdAllocator durationlessAudioIds;
  const auto durationlessAudioPlacement = project::buildMediaPlacementDraft(
    durationlessAudioIds,
    makeDurationlessAsset(
      foundation::AssetId{"asset_audio_without_duration"},
      "Durationless Audio",
      asset::AssetMediaType::Audio,
      "/media/audio.wav"
    ),
    std::nullopt,
    std::nullopt,
    {}
  );
  GRAPPLE_REQUIRE(!durationlessAudioPlacement);
  GRAPPLE_REQUIRE(durationlessAudioPlacement.error().code == "project.asset_duration_missing");
  GRAPPLE_REQUIRE(allUnique({
    project::serializedCommandSourceKind(project::CommandSourceKind::User),
    project::serializedCommandSourceKind(project::CommandSourceKind::Agent),
    project::serializedCommandSourceKind(project::CommandSourceKind::Importer),
    project::serializedCommandSourceKind(project::CommandSourceKind::Migration)
  }));
  GRAPPLE_REQUIRE(allUnique({
    project::serializedEventName(project::EventKind::ProjectCommandApplied),
    project::serializedEventName(project::EventKind::ProjectChanged)
  }));
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
      "Video",
      timeline::TrackKind::Visual
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(createTrack.payload) == project::CommandKind::CreateTrack);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(createTrack.payload) ==
    "{\"nodeId\":\"node_track\",\"compositionNodeId\":\"node_composition\",\"containmentEdgeId\":\"edge_contains_track\",\"name\":\"Video\",\"kind\":\"visual\",\"order\":0}"
  );
  const auto parsedCreateTrackPayload = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::CreateTrack),
    project::serializeCanonicalCommandPayload(createTrack.payload)
  );
  GRAPPLE_REQUIRE(parsedCreateTrackPayload);
  GRAPPLE_REQUIRE(project::commandKind(parsedCreateTrackPayload.value()) == project::CommandKind::CreateTrack);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(parsedCreateTrackPayload.value()) == project::serializeCanonicalCommandPayload(createTrack.payload));
  const auto commandWithUnexpectedField = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::CreateTrack),
    "{\"nodeId\":\"node_track\",\"compositionNodeId\":\"node_composition\",\"containmentEdgeId\":\"edge_contains_track\",\"name\":\"Video\",\"kind\":\"visual\",\"order\":0,\"metadata\":{}}"
  );
  GRAPPLE_REQUIRE(!commandWithUnexpectedField);
  GRAPPLE_REQUIRE(commandWithUnexpectedField.error().message.find("Unexpected serialized field") != std::string::npos);
  const auto commandWithEmptyName = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::CreateComposition),
    "{\"nodeId\":\"node_empty_composition\",\"name\":\"\"}"
  );
  GRAPPLE_REQUIRE(!commandWithEmptyName);
  GRAPPLE_REQUIRE(commandWithEmptyName.error().code == "project.composition_name_empty");

  const auto trackResult = controller.apply(createTrack);
  GRAPPLE_REQUIRE(trackResult);
  GRAPPLE_REQUIRE(trackResult.value().afterRevision == foundation::RevisionId{"rev_2"});

  const auto finalSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(finalSnapshot);
  GRAPPLE_REQUIRE(finalSnapshot.value().graph.nodes().size() == 2);
  GRAPPLE_REQUIRE(finalSnapshot.value().graph.edges().size() == 1);

  const auto compositionQuery = controller.query(project::InspectCompositionsQuery{});
  GRAPPLE_REQUIRE(compositionQuery);
  const auto* compositionResult = std::get_if<project::CompositionInspectResult>(&compositionQuery.value());
  GRAPPLE_REQUIRE(compositionResult != nullptr);
  GRAPPLE_REQUIRE(compositionResult->revision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(compositionResult->compositions.size() == 1);
  GRAPPLE_REQUIRE(compositionResult->compositions[0].nodeId == foundation::NodeId{"node_composition"});
  GRAPPLE_REQUIRE(compositionResult->compositions[0].tracks.size() == 1);
  GRAPPLE_REQUIRE(compositionResult->compositions[0].tracks[0].nodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(compositionResult->compositions[0].tracks[0].name == "Video");
  GRAPPLE_REQUIRE(compositionResult->compositions[0].tracks[0].kind == timeline::TrackKind::Visual);

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
  const std::string serializedWithUnexpectedNodeField = std::string{
    "{\"projectId\":\"proj_test\",\"name\":\"Test Project\",\"revision\":\"rev_3\",\"revisionNumber\":3,"
    "\"settings\":{\"defaultDuration\":null},\"assets\":[],\"graph\":{\"nodes\":["
    "{\"id\":\"node_composition\",\"kind\":\"composition\",\"enabled\":true,"
    "\"payload\":{\"type\":\"composition\",\"name\":\"Main\",\"metadata\":{}}}"
    "],\"edges\":[]}}"
  };
  const auto snapshotWithUnexpectedNodeField = project::deserializeCanonicalProjectSnapshot(serializedWithUnexpectedNodeField);
  GRAPPLE_REQUIRE(!snapshotWithUnexpectedNodeField);
  GRAPPLE_REQUIRE(snapshotWithUnexpectedNodeField.error().message.find("Unexpected serialized field") != std::string::npos);
  const auto snapshotWithEmptyName = project::deserializeCanonicalProjectSnapshot(
    "{\"projectId\":\"proj_test\",\"name\":\"\",\"revision\":\"rev_3\",\"revisionNumber\":3,"
    "\"settings\":{\"defaultDuration\":null},\"assets\":[],\"graph\":{\"nodes\":[],\"edges\":[]}}"
  );
  GRAPPLE_REQUIRE(!snapshotWithEmptyName);
  GRAPPLE_REQUIRE(snapshotWithEmptyName.error().message.find("Expected non-empty string") != std::string::npos);
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
  const auto assetCatalogQuery = assetProject.query(project::GetAssetCatalogQuery{});
  GRAPPLE_REQUIRE(assetCatalogQuery);
  const auto* assetCatalogResult = std::get_if<project::AssetCatalogResult>(&assetCatalogQuery.value());
  GRAPPLE_REQUIRE(assetCatalogResult != nullptr);
  GRAPPLE_REQUIRE(assetCatalogResult->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(assetCatalogResult->assets.assets().size() == 1);
  GRAPPLE_REQUIRE(assetCatalogResult->assets.find(foundation::AssetId{"asset_walking_woman"}) != nullptr);
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
      "Video",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(connectionTrack);

  const project::ProjectCommandEnvelope connectPorts{
    foundation::CommandId{"cmd_connect_ports"},
    foundation::ProjectId{"proj_connection"},
    connectionTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::ConnectPortsCommand{
      foundation::EdgeId{"edge_connect_ports"},
      foundation::NodeId{"node_connection_composition"},
      graph::PortName{"output"},
      foundation::NodeId{"node_connection_track"},
      graph::PortName{"input"},
      2
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(connectPorts.payload) == project::CommandKind::ConnectPorts);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(connectPorts.payload) ==
    "{\"edgeId\":\"edge_connect_ports\",\"sourceNodeId\":\"node_connection_composition\",\"sourcePort\":\"output\",\"targetNodeId\":\"node_connection_track\",\"targetPort\":\"input\",\"order\":2}"
  );
  const auto connectPortsResult = connectionProject.apply(connectPorts);
  GRAPPLE_REQUIRE(connectPortsResult);
  GRAPPLE_REQUIRE(connectPortsResult.value().afterRevision == foundation::RevisionId{"rev_3"});
  const auto connectionSnapshot = connectionProject.snapshot();
  GRAPPLE_REQUIRE(connectionSnapshot);
  GRAPPLE_REQUIRE(connectionSnapshot.value().graph.edges().size() == 2);
  const project::ProjectCommandEnvelope disconnectPorts{
    foundation::CommandId{"cmd_disconnect_ports"},
    foundation::ProjectId{"proj_connection"},
    connectionSnapshot.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DisconnectPortsCommand{foundation::EdgeId{"edge_connect_ports"}}
  };
  GRAPPLE_REQUIRE(project::commandKind(disconnectPorts.payload) == project::CommandKind::DisconnectPorts);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(disconnectPorts.payload) == "{\"edgeId\":\"edge_connect_ports\"}");
  const auto disconnectPortsResult = connectionProject.apply(disconnectPorts);
  GRAPPLE_REQUIRE(disconnectPortsResult);
  GRAPPLE_REQUIRE(disconnectPortsResult.value().afterRevision == foundation::RevisionId{"rev_4"});
  const auto afterDisconnect = connectionProject.snapshot();
  GRAPPLE_REQUIRE(afterDisconnect);
  GRAPPLE_REQUIRE(afterDisconnect.value().graph.edges().size() == 1);
  GRAPPLE_REQUIRE(afterDisconnect.value().graph.edges()[0].id == foundation::EdgeId{"edge_connection_contains_track"});
  const auto disconnectMissing = connectionProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_disconnect_missing"},
    foundation::ProjectId{"proj_connection"},
    afterDisconnect.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DisconnectPortsCommand{foundation::EdgeId{"edge_connect_ports"}}
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
      "Video",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(clipTrack);
  const auto createClipWithMissingAsset = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_clip_missing_asset"},
    foundation::ProjectId{"proj_clip"},
    clipTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip_missing_asset"},
      foundation::NodeId{"node_clip_track"},
      foundation::EdgeId{"edge_clip_contains_missing_asset_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        1.0,
        foundation::AssetId{"asset_missing_clip"},
        timeline::Transform2D{}
      }
    }
  });
  GRAPPLE_REQUIRE(!createClipWithMissingAsset);
  GRAPPLE_REQUIRE(createClipWithMissingAsset.error().code == "project.clip_asset_missing");
  const auto registerClipAsset = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_register_clip_asset"},
    foundation::ProjectId{"proj_clip"},
    clipTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "test"},
    project::RegisterAssetCommand{makeVideoAsset(foundation::AssetId{"asset_clip"}, "Clip")}
  });
  GRAPPLE_REQUIRE(registerClipAsset);
  const auto createClipWithMismatchedTrackKind = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_clip_mismatched_track_kind"},
    foundation::ProjectId{"proj_clip"},
    registerClipAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip_mismatched_track_kind"},
      foundation::NodeId{"node_clip_track"},
      foundation::EdgeId{"edge_clip_contains_mismatched_track_kind"},
      timeline::ClipPayload{
        timeline::ClipKind::Audio,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        1.0,
        foundation::AssetId{"asset_clip"},
        timeline::Transform2D{}
      }
    }
  });
  GRAPPLE_REQUIRE(!createClipWithMismatchedTrackKind);
  GRAPPLE_REQUIRE(createClipWithMismatchedTrackKind.error().code == "project.clip_track_kind_mismatch");
  const auto createClipWithMismatchedAssetKind = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_clip_mismatched_asset_kind"},
    foundation::ProjectId{"proj_clip"},
    registerClipAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip_mismatched_asset_kind"},
      foundation::NodeId{"node_clip_track"},
      foundation::EdgeId{"edge_clip_contains_mismatched_asset_kind"},
      timeline::ClipPayload{
        timeline::ClipKind::Image,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        1.0,
        foundation::AssetId{"asset_clip"},
        timeline::Transform2D{}
      }
    }
  });
  GRAPPLE_REQUIRE(!createClipWithMismatchedAssetKind);
  GRAPPLE_REQUIRE(createClipWithMismatchedAssetKind.error().code == "project.clip_asset_kind_mismatch");
  const timeline::ClipPayload initialClipPayload{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    1.0,
    foundation::AssetId{"asset_clip"},
    timeline::Transform2D{}
  };
  const auto createClip = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_create_clip"},
    foundation::ProjectId{"proj_clip"},
    registerClipAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip"},
      foundation::NodeId{"node_clip_track"},
      foundation::EdgeId{"edge_clip_contains_clip"},
      initialClipPayload
    }
  });
  GRAPPLE_REQUIRE(createClip);

  project::ProjectController mediaProject{project::createEmptyProject(foundation::ProjectId{"proj_media_add"}, "Media Add")};
  const auto registerMediaAsset = mediaProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_register_media_asset"},
    foundation::ProjectId{"proj_media_add"},
    foundation::RevisionId{"rev_0"},
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "test"},
    project::RegisterAssetCommand{makeVideoAsset(foundation::AssetId{"asset_media"}, "Media")}
  });
  GRAPPLE_REQUIRE(registerMediaAsset);
  const project::AddMediaToTimelineCommand addMediaToTimeline{
    project::CreateCompositionCommand{foundation::NodeId{"node_media_composition"}, "Main"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_media_track"},
      foundation::NodeId{"node_media_composition"},
      foundation::EdgeId{"edge_media_contains_track"},
      "Video",
      timeline::TrackKind::Visual
    },
    project::CreateCameraCommand{
      foundation::NodeId{"node_media_camera"},
      foundation::NodeId{"node_media_composition"},
      foundation::EdgeId{"edge_media_contains_camera"},
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    project::CreateClipCommand{
      foundation::NodeId{"node_media_clip"},
      foundation::NodeId{"node_media_track"},
      foundation::EdgeId{"edge_media_contains_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
        1.0,
        foundation::AssetId{"asset_media"},
        timeline::Transform2D{}
      }
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(project::ProjectCommand{addMediaToTimeline}) == project::CommandKind::AddMediaToTimeline);
  const std::string serializedMediaPlacement = project::serializeCanonicalCommandPayload(project::ProjectCommand{addMediaToTimeline});
  GRAPPLE_REQUIRE(serializedMediaPlacement.find("\"composition\":{\"nodeId\":\"node_media_composition\",\"name\":\"Main\"}") != std::string::npos);
  GRAPPLE_REQUIRE(serializedMediaPlacement.find("\"track\":{\"nodeId\":\"node_media_track\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedMediaPlacement.find("\"camera\":{\"nodeId\":\"node_media_camera\"") != std::string::npos);
  GRAPPLE_REQUIRE(serializedMediaPlacement.find("\"clip\":{\"nodeId\":\"node_media_clip\"") != std::string::npos);
  const auto parsedMediaPlacement = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::AddMediaToTimeline),
    serializedMediaPlacement
  );
  GRAPPLE_REQUIRE(parsedMediaPlacement);
  GRAPPLE_REQUIRE(project::commandKind(parsedMediaPlacement.value()) == project::CommandKind::AddMediaToTimeline);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(parsedMediaPlacement.value()) == serializedMediaPlacement);
  const auto mediaPlacementResult = mediaProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_add_media_to_timeline"},
    foundation::ProjectId{"proj_media_add"},
    registerMediaAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    addMediaToTimeline
  });
  GRAPPLE_REQUIRE(mediaPlacementResult);
  GRAPPLE_REQUIRE(mediaPlacementResult.value().beforeRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(mediaPlacementResult.value().afterRevision == foundation::RevisionId{"rev_2"});
  const auto mediaSnapshot = mediaProject.snapshot();
  GRAPPLE_REQUIRE(mediaSnapshot);
  GRAPPLE_REQUIRE(mediaSnapshot.value().graph.hasNode(foundation::NodeId{"node_media_composition"}));
  GRAPPLE_REQUIRE(mediaSnapshot.value().graph.hasNode(foundation::NodeId{"node_media_track"}));
  GRAPPLE_REQUIRE(mediaSnapshot.value().graph.hasNode(foundation::NodeId{"node_media_camera"}));
  GRAPPLE_REQUIRE(mediaSnapshot.value().graph.hasNode(foundation::NodeId{"node_media_clip"}));

  const timeline::Transform2D updatedClipTransform{
    foundation::Vec2{3.0, 4.0},
    foundation::Vec2{2.0, 2.0},
    0.0,
    1.0
  };
  const project::ProjectCommandEnvelope updateClip{
    foundation::CommandId{"cmd_update_clip"},
    foundation::ProjectId{"proj_clip"},
    createClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateClipCommand{
      foundation::NodeId{"node_clip"},
      updatedClipTransform,
      0.5
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(updateClip.payload) == project::CommandKind::UpdateClip);
  const std::string serializedUpdateClip = project::serializeCanonicalCommandPayload(updateClip.payload);
  GRAPPLE_REQUIRE(serializedUpdateClip == "{\"nodeId\":\"node_clip\",\"transform\":{\"position\":{\"x\":3,\"y\":4},\"scale\":{\"x\":2,\"y\":2},\"rotationDegrees\":0,\"opacity\":1},\"playbackRate\":0.5}");
  const auto parsedUpdateClip = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::UpdateClip),
    serializedUpdateClip
  );
  GRAPPLE_REQUIRE(parsedUpdateClip);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(parsedUpdateClip.value()) == serializedUpdateClip);
  const auto updateClipInvalidPlaybackRate = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_update_clip_invalid_playback_rate"},
    foundation::ProjectId{"proj_clip"},
    createClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateClipCommand{
      foundation::NodeId{"node_clip"},
      timeline::Transform2D{},
      0.0
    }
  });
  GRAPPLE_REQUIRE(!updateClipInvalidPlaybackRate);
  GRAPPLE_REQUIRE(updateClipInvalidPlaybackRate.error().code == "project.clip_playback_rate_invalid");
  const auto updateClipResult = clipProject.apply(updateClip);
  GRAPPLE_REQUIRE(updateClipResult);
  GRAPPLE_REQUIRE(updateClipResult.value().afterRevision == foundation::RevisionId{"rev_5"});
  const auto afterClipUpdate = clipProject.snapshot();
  GRAPPLE_REQUIRE(afterClipUpdate);
  const graph::GraphNode* updatedClipNode = afterClipUpdate.value().graph.findNode(foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(updatedClipNode != nullptr);
  const auto* updatedClip = std::get_if<timeline::ClipPayload>(&updatedClipNode->payload);
  GRAPPLE_REQUIRE(updatedClip != nullptr);
  GRAPPLE_REQUIRE(updatedClip->timelineRange.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(updatedClip->timelineRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(updatedClip->sourceRange.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(updatedClip->sourceRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(updatedClip->assetId == foundation::AssetId{"asset_clip"});
  GRAPPLE_REQUIRE(updatedClip->kind == timeline::ClipKind::Video);
  GRAPPLE_REQUIRE(updatedClip->playbackRate == 0.5);
  const projection::TimelineProjector clipProjector;
  const auto clipTimeline = clipProjector.buildTimelineIR(projection::BuildTimelineIRRequest{
    afterClipUpdate.value()
  });
  GRAPPLE_REQUIRE(clipTimeline);
  GRAPPLE_REQUIRE(clipTimeline.value().timeline.duration == foundation::TimeSeconds{5.0});
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
  GRAPPLE_REQUIRE(roundTrippedClip->timelineRange.end == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(roundTrippedClip->sourceRange.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(roundTrippedClip->playbackRate == 0.5);
  GRAPPLE_REQUIRE(roundTrippedClip->transform.position.x == 3.0);
  GRAPPLE_REQUIRE(roundTrippedClip->transform.position.y == 4.0);
  project::ProjectSnapshot invalidRestoredClipSnapshot = afterClipUpdate.value();
  invalidRestoredClipSnapshot.assets = asset::AssetCatalog{};
  const auto restoreInvalidClipSnapshot = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_restore_invalid_clip_snapshot"},
    foundation::ProjectId{"proj_clip"},
    afterClipUpdate.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::RestoreSnapshotCommand{
      foundation::SnapshotId{"snap_invalid_clip"},
      invalidRestoredClipSnapshot
    }
  });
  GRAPPLE_REQUIRE(!restoreInvalidClipSnapshot);
  GRAPPLE_REQUIRE(restoreInvalidClipSnapshot.error().code == "project.snapshot_clip_asset_missing");
  project::ProjectSnapshot invalidRestoredClipKindSnapshot = afterClipUpdate.value();
  const auto replaceInvalidRestoredClipKindPayload = invalidRestoredClipKindSnapshot.graph.replaceNodePayload(
    foundation::NodeId{"node_clip"},
    timeline::ClipPayload{
      timeline::ClipKind::Image,
      foundation::TimeRange{foundation::TimeSeconds{2.0}, foundation::TimeSeconds{12.0}},
      foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{11.0}},
      0.5,
      foundation::AssetId{"asset_clip"},
      timeline::Transform2D{}
    }
  );
  GRAPPLE_REQUIRE(replaceInvalidRestoredClipKindPayload);
  const auto restoreInvalidClipKindSnapshot = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_restore_invalid_clip_kind_snapshot"},
    foundation::ProjectId{"proj_clip"},
    afterClipUpdate.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::RestoreSnapshotCommand{
      foundation::SnapshotId{"snap_invalid_clip_kind"},
      invalidRestoredClipKindSnapshot
    }
  });
  GRAPPLE_REQUIRE(!restoreInvalidClipKindSnapshot);
  GRAPPLE_REQUIRE(restoreInvalidClipKindSnapshot.error().code == "project.snapshot_clip_asset_kind_mismatch");
  const auto afterInvalidRestore = clipProject.snapshot();
  GRAPPLE_REQUIRE(afterInvalidRestore);
  GRAPPLE_REQUIRE(afterInvalidRestore.value().revision == afterClipUpdate.value().revision);
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
  GRAPPLE_REQUIRE(deleteClipResult.value().afterRevision == foundation::RevisionId{"rev_6"});
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
      updatedClipTransform,
      0.5
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

  const auto recreateClip = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_recreate_clip_before_track_delete"},
    foundation::ProjectId{"proj_clip"},
    afterClipDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip_recreated"},
      foundation::NodeId{"node_clip_track"},
      foundation::EdgeId{"edge_clip_contains_recreated_clip"},
      initialClipPayload
    }
  });
  GRAPPLE_REQUIRE(recreateClip);
  GRAPPLE_REQUIRE(recreateClip.value().afterRevision == foundation::RevisionId{"rev_7"});
  const project::ProjectCommandEnvelope deleteTrack{
    foundation::CommandId{"cmd_delete_track"},
    foundation::ProjectId{"proj_clip"},
    recreateClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteTrackCommand{foundation::NodeId{"node_clip_track"}}
  };
  GRAPPLE_REQUIRE(project::commandKind(deleteTrack.payload) == project::CommandKind::DeleteTrack);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(deleteTrack.payload) == "{\"nodeId\":\"node_clip_track\"}");
  const auto parsedDeleteTrackPayload = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::DeleteTrack),
    project::serializeCanonicalCommandPayload(deleteTrack.payload)
  );
  GRAPPLE_REQUIRE(parsedDeleteTrackPayload);
  GRAPPLE_REQUIRE(project::commandKind(parsedDeleteTrackPayload.value()) == project::CommandKind::DeleteTrack);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(parsedDeleteTrackPayload.value()) == project::serializeCanonicalCommandPayload(deleteTrack.payload));
  const auto deleteTrackResult = clipProject.apply(deleteTrack);
  GRAPPLE_REQUIRE(deleteTrackResult);
  GRAPPLE_REQUIRE(deleteTrackResult.value().afterRevision == foundation::RevisionId{"rev_8"});
  const auto afterTrackDelete = clipProject.snapshot();
  GRAPPLE_REQUIRE(afterTrackDelete);
  GRAPPLE_REQUIRE(!afterTrackDelete.value().graph.hasNode(foundation::NodeId{"node_clip_track"}));
  GRAPPLE_REQUIRE(!afterTrackDelete.value().graph.hasNode(foundation::NodeId{"node_clip_recreated"}));
  GRAPPLE_REQUIRE(afterTrackDelete.value().graph.edges().empty());
  const auto trackDeletedTimeline = clipProjector.buildTimelineIR(projection::BuildTimelineIRRequest{
    afterTrackDelete.value()
  });
  GRAPPLE_REQUIRE(trackDeletedTimeline);
  GRAPPLE_REQUIRE(trackDeletedTimeline.value().timeline.layers.empty());
  GRAPPLE_REQUIRE(trackDeletedTimeline.value().timeline.clips.empty());
  const auto deleteMissingTrack = clipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_delete_missing_track"},
    foundation::ProjectId{"proj_clip"},
    afterTrackDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteTrackCommand{foundation::NodeId{"node_clip_track"}}
  });
  GRAPPLE_REQUIRE(!deleteMissingTrack);
  GRAPPLE_REQUIRE(deleteMissingTrack.error().code == "project.track_missing");

  project::ProjectController moveClipProject{
    project::createEmptyProject(foundation::ProjectId{"proj_move_clip"}, "Move Clip Project")
  };
  const auto moveClipInitial = moveClipProject.snapshot();
  GRAPPLE_REQUIRE(moveClipInitial);
  const auto moveClipComposition = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_move_clip_composition"},
    foundation::ProjectId{"proj_move_clip"},
    moveClipInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_move_clip_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(moveClipComposition);
  const auto moveClipTrack = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_move_clip_track"},
    foundation::ProjectId{"proj_move_clip"},
    moveClipComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_move_clip_track"},
      foundation::NodeId{"node_move_clip_composition"},
      foundation::EdgeId{"edge_move_clip_contains_track"},
      "Video",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(moveClipTrack);
  const auto registerMoveClipAsset = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_register_move_clip_asset"},
    foundation::ProjectId{"proj_move_clip"},
    moveClipTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "test"},
    project::RegisterAssetCommand{makeVideoAsset(foundation::AssetId{"asset_move_clip"}, "Move Clip")}
  });
  GRAPPLE_REQUIRE(registerMoveClipAsset);
  const auto moveClipCreate = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_move_clip_create"},
    foundation::ProjectId{"proj_move_clip"},
    registerMoveClipAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_move_clip"},
      foundation::NodeId{"node_move_clip_track"},
      foundation::EdgeId{"edge_move_clip_contains_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{6.0}},
        foundation::TimeRange{foundation::TimeSeconds{3.0}, foundation::TimeSeconds{8.0}},
        0.5,
        foundation::AssetId{"asset_move_clip"},
        timeline::Transform2D{
          foundation::Vec2{2.0, 3.0},
          foundation::Vec2{1.5, 1.5},
          10.0,
          0.75
        }
      }
    }
  });
  GRAPPLE_REQUIRE(moveClipCreate);
  const project::ProjectCommandEnvelope moveClip{
    foundation::CommandId{"cmd_move_clip"},
    foundation::ProjectId{"proj_move_clip"},
    moveClipCreate.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::MoveClipCommand{foundation::NodeId{"node_move_clip"}, foundation::TimeSeconds{4.0}}
  };
  GRAPPLE_REQUIRE(project::commandKind(moveClip.payload) == project::CommandKind::MoveClip);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(moveClip.payload) == "{\"nodeId\":\"node_move_clip\",\"newStart\":4}");
  const auto moveClipResult = moveClipProject.apply(moveClip);
  GRAPPLE_REQUIRE(moveClipResult);
  GRAPPLE_REQUIRE(moveClipResult.value().afterRevision == foundation::RevisionId{"rev_5"});
  const auto afterMoveClip = moveClipProject.snapshot();
  GRAPPLE_REQUIRE(afterMoveClip);
  const graph::GraphNode* movedClipNode = afterMoveClip.value().graph.findNode(foundation::NodeId{"node_move_clip"});
  GRAPPLE_REQUIRE(movedClipNode != nullptr);
  const auto* movedClip = std::get_if<timeline::ClipPayload>(&movedClipNode->payload);
  GRAPPLE_REQUIRE(movedClip != nullptr);
  GRAPPLE_REQUIRE(movedClip->timelineRange.start == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(movedClip->timelineRange.end == foundation::TimeSeconds{9.0});
  GRAPPLE_REQUIRE(movedClip->sourceRange.start == foundation::TimeSeconds{3.0});
  GRAPPLE_REQUIRE(movedClip->sourceRange.end == foundation::TimeSeconds{8.0});
  GRAPPLE_REQUIRE(movedClip->playbackRate == 0.5);
  GRAPPLE_REQUIRE(movedClip->assetId == foundation::AssetId{"asset_move_clip"});
  GRAPPLE_REQUIRE(movedClip->transform.position.x == 2.0);
  GRAPPLE_REQUIRE(movedClip->transform.opacity == 0.75);
  const project::ProjectCommandEnvelope trimClip{
    foundation::CommandId{"cmd_trim_clip"},
    foundation::ProjectId{"proj_move_clip"},
    afterMoveClip.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::TrimClipCommand{
      foundation::NodeId{"node_move_clip"},
      foundation::TimeRange{foundation::TimeSeconds{5.0}, foundation::TimeSeconds{8.0}},
      foundation::TimeRange{foundation::TimeSeconds{4.0}, foundation::TimeSeconds{7.0}}
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(trimClip.payload) == project::CommandKind::TrimClip);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(trimClip.payload) == "{\"nodeId\":\"node_move_clip\",\"timelineRange\":{\"start\":5,\"end\":8},\"sourceRange\":{\"start\":4,\"end\":7}}");
  const auto trimClipResult = moveClipProject.apply(trimClip);
  GRAPPLE_REQUIRE(trimClipResult);
  GRAPPLE_REQUIRE(trimClipResult.value().afterRevision == foundation::RevisionId{"rev_6"});
  const auto afterTrimClip = moveClipProject.snapshot();
  GRAPPLE_REQUIRE(afterTrimClip);
  const graph::GraphNode* trimmedClipNode = afterTrimClip.value().graph.findNode(foundation::NodeId{"node_move_clip"});
  GRAPPLE_REQUIRE(trimmedClipNode != nullptr);
  const auto* trimmedClip = std::get_if<timeline::ClipPayload>(&trimmedClipNode->payload);
  GRAPPLE_REQUIRE(trimmedClip != nullptr);
  GRAPPLE_REQUIRE(trimmedClip->timelineRange.start == foundation::TimeSeconds{5.0});
  GRAPPLE_REQUIRE(trimmedClip->timelineRange.end == foundation::TimeSeconds{8.0});
  GRAPPLE_REQUIRE(trimmedClip->sourceRange.start == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(trimmedClip->sourceRange.end == foundation::TimeSeconds{7.0});
  GRAPPLE_REQUIRE(trimmedClip->playbackRate == 0.5);
  GRAPPLE_REQUIRE(trimmedClip->assetId == foundation::AssetId{"asset_move_clip"});
  GRAPPLE_REQUIRE(trimmedClip->transform.position.x == 2.0);
  const auto moveClipBeforeZero = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_move_clip_before_zero"},
    foundation::ProjectId{"proj_move_clip"},
    afterTrimClip.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::MoveClipCommand{foundation::NodeId{"node_move_clip"}, foundation::TimeSeconds{-1.0}}
  });
  GRAPPLE_REQUIRE(!moveClipBeforeZero);
  GRAPPLE_REQUIRE(moveClipBeforeZero.error().code == "project.clip_move_before_zero");
  const auto moveMissingClip = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_move_missing_clip"},
    foundation::ProjectId{"proj_move_clip"},
    afterTrimClip.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::MoveClipCommand{foundation::NodeId{"node_missing_clip"}, foundation::TimeSeconds{0.0}}
  });
  GRAPPLE_REQUIRE(!moveMissingClip);
  GRAPPLE_REQUIRE(moveMissingClip.error().code == "project.clip_missing");
  const auto trimClipInvalidTimelineRange = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_trim_clip_invalid_timeline"},
    foundation::ProjectId{"proj_move_clip"},
    afterTrimClip.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::TrimClipCommand{
      foundation::NodeId{"node_move_clip"},
      foundation::TimeRange{foundation::TimeSeconds{8.0}, foundation::TimeSeconds{5.0}},
      foundation::TimeRange{foundation::TimeSeconds{4.0}, foundation::TimeSeconds{7.0}}
    }
  });
  GRAPPLE_REQUIRE(!trimClipInvalidTimelineRange);
  GRAPPLE_REQUIRE(trimClipInvalidTimelineRange.error().code == "project.clip_timeline_range_invalid");
  const auto trimClipZeroTimelineRange = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_trim_clip_zero_timeline"},
    foundation::ProjectId{"proj_move_clip"},
    afterTrimClip.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::TrimClipCommand{
      foundation::NodeId{"node_move_clip"},
      foundation::TimeRange{foundation::TimeSeconds{5.0}, foundation::TimeSeconds{5.0}},
      foundation::TimeRange{foundation::TimeSeconds{4.0}, foundation::TimeSeconds{7.0}}
    }
  });
  GRAPPLE_REQUIRE(!trimClipZeroTimelineRange);
  GRAPPLE_REQUIRE(trimClipZeroTimelineRange.error().code == "project.clip_timeline_range_invalid");
  const auto trimClipZeroSourceRange = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_trim_clip_zero_source"},
    foundation::ProjectId{"proj_move_clip"},
    afterTrimClip.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::TrimClipCommand{
      foundation::NodeId{"node_move_clip"},
      foundation::TimeRange{foundation::TimeSeconds{5.0}, foundation::TimeSeconds{8.0}},
      foundation::TimeRange{foundation::TimeSeconds{4.0}, foundation::TimeSeconds{4.0}}
    }
  });
  GRAPPLE_REQUIRE(!trimClipZeroSourceRange);
  GRAPPLE_REQUIRE(trimClipZeroSourceRange.error().code == "project.clip_source_range_invalid");
  const auto trimMissingClip = moveClipProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_trim_missing_clip"},
    foundation::ProjectId{"proj_move_clip"},
    afterTrimClip.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::TrimClipCommand{
      foundation::NodeId{"node_missing_clip"},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
    }
  });
  GRAPPLE_REQUIRE(!trimMissingClip);
  GRAPPLE_REQUIRE(trimMissingClip.error().code == "project.clip_missing");

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
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
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
        timeline::CameraState{
          timeline::Transform2D{
            foundation::Vec2{1.0, 2.0},
            foundation::Vec2{1.5, 1.5},
            12.0,
            0.8
          },
          timeline::CameraLens{85.0}
        }
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
  GRAPPLE_REQUIRE(updatedCameraPayload->state.lens.focalLength == 85.0);
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
  GRAPPLE_REQUIRE(roundTrippedCamera->state.transform.rotationDegrees == 12.0);
  GRAPPLE_REQUIRE(roundTrippedCamera->state.lens.focalLength == 85.0);
  const auto oldShapeCameraSnapshot = project::deserializeCanonicalProjectSnapshot(
    "{\"projectId\":\"proj_camera\",\"name\":\"Camera Project\",\"revision\":\"rev_3\",\"revisionNumber\":3,"
    "\"settings\":{\"defaultDuration\":null},\"assets\":[],\"graph\":{\"nodes\":["
    "{\"id\":\"node_camera\",\"kind\":\"camera\",\"enabled\":true,\"payload\":{\"type\":\"camera\",\"payload\":{"
    "\"name\":\"Camera\","
    "\"transform\":{\"position\":{\"x\":0,\"y\":0},\"scale\":{\"x\":1,\"y\":1},\"rotationDegrees\":0,\"opacity\":1},"
    "\"lens\":{\"focalLength\":35}"
    "}}}"
    "],\"edges\":[]}}"
  );
  GRAPPLE_REQUIRE(!oldShapeCameraSnapshot);
  GRAPPLE_REQUIRE(oldShapeCameraSnapshot.error().message.find("Unexpected serialized field") != std::string::npos);
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
        },
        {
          timeline::Param::Keyframe{
            foundation::KeyframeId{"key_smoothing_1"},
            foundation::TimeSeconds{0.5},
            0.75
          }
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
      "Video",
      timeline::TrackKind::Visual
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
  timeline::EffectPayload emptyParamNameEffectPayload = projectEffectPayload;
  emptyParamNameEffectPayload.params.values[0].name = "";
  const auto emptyParamNameEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_empty_param_name_effect"},
    foundation::ProjectId{"proj_effect"},
    effectTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_empty_param_name_effect"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_empty_param_name_effect_target"},
      emptyParamNameEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(!emptyParamNameEffect);
  GRAPPLE_REQUIRE(emptyParamNameEffect.error().code == "project.effect_param_name_empty");
  const auto emptyEffectTargetPort = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_empty_effect_target_port"},
    foundation::ProjectId{"proj_effect"},
    effectTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_empty_effect_target_port"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_empty_effect_target_port"},
      projectEffectPayload,
      graph::PortName{},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(!emptyEffectTargetPort);
  GRAPPLE_REQUIRE(emptyEffectTargetPort.error().code == "project.effect_target_port_empty");
  timeline::EffectPayload nonNumericEffectPayload = projectEffectPayload;
  nonNumericEffectPayload.params.values[0].name = "enabled";
  nonNumericEffectPayload.params.values[0].value = true;
  nonNumericEffectPayload.params.values[0].control.numeric = std::nullopt;
  const auto nonNumericAgentEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_non_numeric_agent_effect"},
    foundation::ProjectId{"proj_effect"},
    effectTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_effect"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_non_numeric_agent_effect"},
      foundation::NodeId{"node_effect_track"},
      foundation::EdgeId{"edge_non_numeric_agent_effect_target"},
      nonNumericEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(nonNumericAgentEffect);
  const auto afterNonNumericAgentEffect = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterNonNumericAgentEffect);
  GRAPPLE_REQUIRE(afterNonNumericAgentEffect.value().graph.hasNode(foundation::NodeId{"node_non_numeric_agent_effect"}));
  const auto userParameterlessEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_user_parameterless_effect"},
    foundation::ProjectId{"proj_effect"},
    nonNumericAgentEffect.value().afterRevision,
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
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].keyframes[0].id == foundation::KeyframeId{"key_smoothing_1"});
  GRAPPLE_REQUIRE(roundTrippedEffect->params.values[0].keyframes[0].time == foundation::TimeSeconds{0.5});
  GRAPPLE_REQUIRE(std::get<double>(roundTrippedEffect->params.values[0].keyframes[0].value) == 0.75);
  GRAPPLE_REQUIRE(roundTrippedEffect->activeRange.end == foundation::TimeSeconds{1.0});

  const project::ProjectCommandEnvelope updateEffectParamValue{
    foundation::CommandId{"cmd_update_effect_param_value"},
    foundation::ProjectId{"proj_effect"},
    afterTrackEffect.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateEffectParamValueCommand{
      foundation::NodeId{"node_track_effect"},
      "smoothing",
      0.4
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(updateEffectParamValue.payload) == project::CommandKind::UpdateEffectParamValue);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(updateEffectParamValue.payload) ==
    "{\"effectNodeId\":\"node_track_effect\",\"paramName\":\"smoothing\",\"value\":0.40000000000000002}"
  );
  const auto parsedUpdateEffectParamValue = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::UpdateEffectParamValue),
    project::serializeCanonicalCommandPayload(updateEffectParamValue.payload)
  );
  GRAPPLE_REQUIRE(parsedUpdateEffectParamValue);
  GRAPPLE_REQUIRE(project::commandKind(parsedUpdateEffectParamValue.value()) == project::CommandKind::UpdateEffectParamValue);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(parsedUpdateEffectParamValue.value()) == project::serializeCanonicalCommandPayload(updateEffectParamValue.payload));
  const auto updateEffectParamValueResult = effectProject.apply(updateEffectParamValue);
  GRAPPLE_REQUIRE(updateEffectParamValueResult);
  const auto afterParamValueUpdate = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterParamValueUpdate);
  const graph::GraphNode* valueUpdatedEffectNode = afterParamValueUpdate.value().graph.findNode(foundation::NodeId{"node_track_effect"});
  GRAPPLE_REQUIRE(valueUpdatedEffectNode != nullptr);
  const auto* valueUpdatedEffect = std::get_if<timeline::EffectPayload>(&valueUpdatedEffectNode->payload);
  GRAPPLE_REQUIRE(valueUpdatedEffect != nullptr);
  GRAPPLE_REQUIRE(std::get<double>(valueUpdatedEffect->params.values[0].value) == 0.4);
  GRAPPLE_REQUIRE(valueUpdatedEffect->params.values[0].control.label == "Smoothing");
  GRAPPLE_REQUIRE(valueUpdatedEffect->params.values[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(valueUpdatedEffect->params.values[0].keyframes[0].id == foundation::KeyframeId{"key_smoothing_1"});

  const auto mismatchedParamValue = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_mismatched_effect_param_value"},
    foundation::ProjectId{"proj_effect"},
    afterParamValueUpdate.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateEffectParamValueCommand{
      foundation::NodeId{"node_track_effect"},
      "smoothing",
      true
    }
  });
  GRAPPLE_REQUIRE(!mismatchedParamValue);
  GRAPPLE_REQUIRE(mismatchedParamValue.error().code == "project.effect_param_value_type_mismatch");

  const project::ProjectCommandEnvelope upsertEffectKeyframe{
    foundation::CommandId{"cmd_upsert_effect_keyframe"},
    foundation::ProjectId{"proj_effect"},
    afterParamValueUpdate.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpsertEffectParamKeyframeCommand{
      foundation::NodeId{"node_track_effect"},
      "smoothing",
      timeline::Param::Keyframe{
        foundation::KeyframeId{"key_smoothing_2"},
        foundation::TimeSeconds{0.75},
        0.5
      }
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(upsertEffectKeyframe.payload) == project::CommandKind::UpsertEffectParamKeyframe);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(upsertEffectKeyframe.payload) ==
    "{\"effectNodeId\":\"node_track_effect\",\"paramName\":\"smoothing\",\"keyframe\":{\"id\":\"key_smoothing_2\",\"time\":0.75,\"value\":0.5}}"
  );
  const auto parsedUpsertEffectKeyframe = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::UpsertEffectParamKeyframe),
    project::serializeCanonicalCommandPayload(upsertEffectKeyframe.payload)
  );
  GRAPPLE_REQUIRE(parsedUpsertEffectKeyframe);
  GRAPPLE_REQUIRE(project::commandKind(parsedUpsertEffectKeyframe.value()) == project::CommandKind::UpsertEffectParamKeyframe);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(parsedUpsertEffectKeyframe.value()) == project::serializeCanonicalCommandPayload(upsertEffectKeyframe.payload));
  const auto upsertEffectKeyframeResult = effectProject.apply(upsertEffectKeyframe);
  GRAPPLE_REQUIRE(upsertEffectKeyframeResult);
  const auto afterKeyframeUpsert = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterKeyframeUpsert);
  const graph::GraphNode* keyframedEffectNode = afterKeyframeUpsert.value().graph.findNode(foundation::NodeId{"node_track_effect"});
  GRAPPLE_REQUIRE(keyframedEffectNode != nullptr);
  const auto* keyframedEffect = std::get_if<timeline::EffectPayload>(&keyframedEffectNode->payload);
  GRAPPLE_REQUIRE(keyframedEffect != nullptr);
  GRAPPLE_REQUIRE(keyframedEffect->params.values[0].keyframes.size() == 2);

  const auto replaceEffectKeyframe = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_replace_effect_keyframe"},
    foundation::ProjectId{"proj_effect"},
    afterKeyframeUpsert.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpsertEffectParamKeyframeCommand{
      foundation::NodeId{"node_track_effect"},
      "smoothing",
      timeline::Param::Keyframe{
        foundation::KeyframeId{"key_smoothing_2"},
        foundation::TimeSeconds{0.9},
        0.6
      }
    }
  });
  GRAPPLE_REQUIRE(replaceEffectKeyframe);
  const auto afterKeyframeReplace = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterKeyframeReplace);
  const graph::GraphNode* replacedKeyframeEffectNode = afterKeyframeReplace.value().graph.findNode(foundation::NodeId{"node_track_effect"});
  GRAPPLE_REQUIRE(replacedKeyframeEffectNode != nullptr);
  const auto* replacedKeyframeEffect = std::get_if<timeline::EffectPayload>(&replacedKeyframeEffectNode->payload);
  GRAPPLE_REQUIRE(replacedKeyframeEffect != nullptr);
  GRAPPLE_REQUIRE(replacedKeyframeEffect->params.values[0].keyframes.size() == 2);
  const auto replacedKeyframe = std::find_if(
    replacedKeyframeEffect->params.values[0].keyframes.begin(),
    replacedKeyframeEffect->params.values[0].keyframes.end(),
    [](const timeline::Param::Keyframe& keyframe) {
      return keyframe.id == foundation::KeyframeId{"key_smoothing_2"};
    }
  );
  GRAPPLE_REQUIRE(replacedKeyframe != replacedKeyframeEffect->params.values[0].keyframes.end());
  GRAPPLE_REQUIRE(replacedKeyframe->time == foundation::TimeSeconds{0.9});
  GRAPPLE_REQUIRE(std::get<double>(replacedKeyframe->value) == 0.6);

  const auto mismatchedKeyframeValue = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_mismatched_effect_keyframe"},
    foundation::ProjectId{"proj_effect"},
    afterKeyframeReplace.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpsertEffectParamKeyframeCommand{
      foundation::NodeId{"node_track_effect"},
      "smoothing",
      timeline::Param::Keyframe{
        foundation::KeyframeId{"key_smoothing_bad"},
        foundation::TimeSeconds{0.9},
        true
      }
    }
  });
  GRAPPLE_REQUIRE(!mismatchedKeyframeValue);
  GRAPPLE_REQUIRE(mismatchedKeyframeValue.error().code == "project.effect_keyframe_value_type_mismatch");

  const project::ProjectCommandEnvelope deleteEffectKeyframe{
    foundation::CommandId{"cmd_delete_effect_keyframe"},
    foundation::ProjectId{"proj_effect"},
    afterKeyframeReplace.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteEffectParamKeyframeCommand{
      foundation::NodeId{"node_track_effect"},
      "smoothing",
      foundation::KeyframeId{"key_smoothing_2"}
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(deleteEffectKeyframe.payload) == project::CommandKind::DeleteEffectParamKeyframe);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalCommandPayload(deleteEffectKeyframe.payload) ==
    "{\"effectNodeId\":\"node_track_effect\",\"paramName\":\"smoothing\",\"keyframeId\":\"key_smoothing_2\"}"
  );
  const auto parsedDeleteEffectKeyframe = project::deserializeCanonicalCommandPayload(
    project::serializedCommandName(project::CommandKind::DeleteEffectParamKeyframe),
    project::serializeCanonicalCommandPayload(deleteEffectKeyframe.payload)
  );
  GRAPPLE_REQUIRE(parsedDeleteEffectKeyframe);
  GRAPPLE_REQUIRE(project::commandKind(parsedDeleteEffectKeyframe.value()) == project::CommandKind::DeleteEffectParamKeyframe);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(parsedDeleteEffectKeyframe.value()) == project::serializeCanonicalCommandPayload(deleteEffectKeyframe.payload));
  const auto deleteEffectKeyframeResult = effectProject.apply(deleteEffectKeyframe);
  GRAPPLE_REQUIRE(deleteEffectKeyframeResult);
  const auto afterKeyframeDelete = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterKeyframeDelete);
  const graph::GraphNode* deletedKeyframeEffectNode = afterKeyframeDelete.value().graph.findNode(foundation::NodeId{"node_track_effect"});
  GRAPPLE_REQUIRE(deletedKeyframeEffectNode != nullptr);
  const auto* deletedKeyframeEffect = std::get_if<timeline::EffectPayload>(&deletedKeyframeEffectNode->payload);
  GRAPPLE_REQUIRE(deletedKeyframeEffect != nullptr);
  GRAPPLE_REQUIRE(deletedKeyframeEffect->params.values[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(deletedKeyframeEffect->params.values[0].keyframes[0].id == foundation::KeyframeId{"key_smoothing_1"});

  const project::ProjectCommandEnvelope deleteEffect{
    foundation::CommandId{"cmd_delete_track_effect"},
    foundation::ProjectId{"proj_effect"},
    afterKeyframeDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteEffectCommand{foundation::NodeId{"node_track_effect"}}
  };
  GRAPPLE_REQUIRE(project::commandKind(deleteEffect.payload) == project::CommandKind::DeleteEffect);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(deleteEffect.payload) == "{\"nodeId\":\"node_track_effect\"}");
  const auto deleteEffectResult = effectProject.apply(deleteEffect);
  GRAPPLE_REQUIRE(deleteEffectResult);
  const auto afterEffectDelete = effectProject.snapshot();
  GRAPPLE_REQUIRE(afterEffectDelete);
  GRAPPLE_REQUIRE(!afterEffectDelete.value().graph.hasNode(foundation::NodeId{"node_track_effect"}));
  const auto removedTargetEdge = std::find_if(
    afterEffectDelete.value().graph.edges().begin(),
    afterEffectDelete.value().graph.edges().end(),
    [](const graph::GraphEdge& edge) {
      return edge.id == foundation::EdgeId{"edge_track_effect_target"};
    }
  );
  GRAPPLE_REQUIRE(removedTargetEdge == afterEffectDelete.value().graph.edges().end());
  const auto deleteMissingEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_delete_missing_effect"},
    foundation::ProjectId{"proj_effect"},
    afterEffectDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteEffectCommand{foundation::NodeId{"node_missing_effect"}}
  });
  GRAPPLE_REQUIRE(!deleteMissingEffect);
  GRAPPLE_REQUIRE(deleteMissingEffect.error().code == "project.effect_missing");
  const auto deleteNonEffect = effectProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_delete_non_effect"},
    foundation::ProjectId{"proj_effect"},
    afterEffectDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteEffectCommand{foundation::NodeId{"node_effect_track"}}
  });
  GRAPPLE_REQUIRE(!deleteNonEffect);
  GRAPPLE_REQUIRE(deleteNonEffect.error().code == "project.effect_missing");

  project::ProjectController targetDeleteProject{
    project::createEmptyProject(foundation::ProjectId{"proj_target_delete"}, "Target Delete Project")
  };
  const auto targetDeleteInitial = targetDeleteProject.snapshot();
  GRAPPLE_REQUIRE(targetDeleteInitial);
  const auto targetDeleteComposition = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_target_delete_composition"},
    foundation::ProjectId{"proj_target_delete"},
    targetDeleteInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_target_delete_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(targetDeleteComposition);
  const auto targetDeleteTrack = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_target_delete_track"},
    foundation::ProjectId{"proj_target_delete"},
    targetDeleteComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_target_delete_track"},
      foundation::NodeId{"node_target_delete_composition"},
      foundation::EdgeId{"edge_target_delete_contains_track"},
      "Video",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(targetDeleteTrack);
  const auto targetDeleteAsset = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_target_delete_asset"},
    foundation::ProjectId{"proj_target_delete"},
    targetDeleteTrack.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "test"},
    project::RegisterAssetCommand{makeVideoAsset(foundation::AssetId{"asset_target_delete"}, "Target Delete")}
  });
  GRAPPLE_REQUIRE(targetDeleteAsset);
  const timeline::ClipPayload targetDeleteClipPayload{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}},
    1.0,
    foundation::AssetId{"asset_target_delete"},
    timeline::Transform2D{}
  };
  const auto targetDeleteClip = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_target_delete_clip"},
    foundation::ProjectId{"proj_target_delete"},
    targetDeleteAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_target_delete_clip"},
      foundation::NodeId{"node_target_delete_track"},
      foundation::EdgeId{"edge_target_delete_contains_clip"},
      targetDeleteClipPayload
    }
  });
  GRAPPLE_REQUIRE(targetDeleteClip);
  const auto targetDeleteClipEffect = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_target_delete_clip_effect"},
    foundation::ProjectId{"proj_target_delete"},
    targetDeleteClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_target_delete_clip_effect"},
      foundation::NodeId{"node_target_delete_clip"},
      foundation::EdgeId{"edge_target_delete_clip_effect_target"},
      projectEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(targetDeleteClipEffect);
  const auto deleteTargetClip = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_delete_target_clip"},
    foundation::ProjectId{"proj_target_delete"},
    targetDeleteClipEffect.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteClipCommand{foundation::NodeId{"node_target_delete_clip"}}
  });
  GRAPPLE_REQUIRE(deleteTargetClip);
  const auto afterTargetClipDelete = targetDeleteProject.snapshot();
  GRAPPLE_REQUIRE(afterTargetClipDelete);
  GRAPPLE_REQUIRE(!afterTargetClipDelete.value().graph.hasNode(foundation::NodeId{"node_target_delete_clip"}));
  GRAPPLE_REQUIRE(!afterTargetClipDelete.value().graph.hasNode(foundation::NodeId{"node_target_delete_clip_effect"}));
  const projection::TimelineProjector targetDeleteProjector;
  const auto clipTargetDeletedTimeline = targetDeleteProjector.buildTimelineIR(projection::BuildTimelineIRRequest{
    afterTargetClipDelete.value()
  });
  GRAPPLE_REQUIRE(clipTargetDeletedTimeline);
  GRAPPLE_REQUIRE(clipTargetDeletedTimeline.value().timeline.effectGraphs.empty());

  const auto recreatedTargetDeleteClip = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_recreate_target_delete_clip"},
    foundation::ProjectId{"proj_target_delete"},
    afterTargetClipDelete.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateClipCommand{
      foundation::NodeId{"node_target_delete_clip_recreated"},
      foundation::NodeId{"node_target_delete_track"},
      foundation::EdgeId{"edge_target_delete_contains_recreated_clip"},
      targetDeleteClipPayload
    }
  });
  GRAPPLE_REQUIRE(recreatedTargetDeleteClip);
  const auto targetDeleteTrackEffect = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_target_delete_track_effect"},
    foundation::ProjectId{"proj_target_delete"},
    recreatedTargetDeleteClip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_target_delete_track_effect"},
      foundation::NodeId{"node_target_delete_track"},
      foundation::EdgeId{"edge_target_delete_track_effect_target"},
      projectEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(targetDeleteTrackEffect);
  const auto recreatedTargetDeleteClipEffect = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_recreated_target_delete_clip_effect"},
    foundation::ProjectId{"proj_target_delete"},
    targetDeleteTrackEffect.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_recreated_target_delete_clip_effect"},
      foundation::NodeId{"node_target_delete_clip_recreated"},
      foundation::EdgeId{"edge_recreated_target_delete_clip_effect_target"},
      projectEffectPayload,
      graph::PortName{"output"},
      graph::PortName{"input"}
    }
  });
  GRAPPLE_REQUIRE(recreatedTargetDeleteClipEffect);
  const auto deleteTargetTrack = targetDeleteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_delete_target_track"},
    foundation::ProjectId{"proj_target_delete"},
    recreatedTargetDeleteClipEffect.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::DeleteTrackCommand{foundation::NodeId{"node_target_delete_track"}}
  });
  GRAPPLE_REQUIRE(deleteTargetTrack);
  const auto afterTargetTrackDelete = targetDeleteProject.snapshot();
  GRAPPLE_REQUIRE(afterTargetTrackDelete);
  GRAPPLE_REQUIRE(!afterTargetTrackDelete.value().graph.hasNode(foundation::NodeId{"node_target_delete_track"}));
  GRAPPLE_REQUIRE(!afterTargetTrackDelete.value().graph.hasNode(foundation::NodeId{"node_target_delete_clip_recreated"}));
  GRAPPLE_REQUIRE(!afterTargetTrackDelete.value().graph.hasNode(foundation::NodeId{"node_target_delete_track_effect"}));
  GRAPPLE_REQUIRE(!afterTargetTrackDelete.value().graph.hasNode(foundation::NodeId{"node_recreated_target_delete_clip_effect"}));
  GRAPPLE_REQUIRE(afterTargetTrackDelete.value().graph.edges().empty());
  const auto trackTargetDeletedTimeline = targetDeleteProjector.buildTimelineIR(projection::BuildTimelineIRRequest{
    afterTargetTrackDelete.value()
  });
  GRAPPLE_REQUIRE(trackTargetDeletedTimeline);
  GRAPPLE_REQUIRE(trackTargetDeletedTimeline.value().timeline.layers.empty());
  GRAPPLE_REQUIRE(trackTargetDeletedTimeline.value().timeline.effectGraphs.empty());

  project::ProjectController noteProject{
    project::createEmptyProject(foundation::ProjectId{"proj_notes"}, "Notes Project")
  };
  const auto noteInitial = noteProject.snapshot();
  GRAPPLE_REQUIRE(noteInitial);
  const project::ProjectCommandEnvelope createNote{
    foundation::CommandId{"cmd_create_note"},
    foundation::ProjectId{"proj_notes"},
    noteInitial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateNoteCommand{
      foundation::NodeId{"node_note"},
      timeline::NotePayload{"Framing note", "Keep the subject editable."}
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(createNote.payload) == project::CommandKind::CreateNote);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(createNote.payload) == "{\"nodeId\":\"node_note\",\"title\":\"Framing note\",\"markdown\":\"Keep the subject editable.\"}");
  const auto createNoteResult = noteProject.apply(createNote);
  GRAPPLE_REQUIRE(createNoteResult);
  GRAPPLE_REQUIRE(createNoteResult.value().afterRevision == foundation::RevisionId{"rev_1"});
  const auto afterCreateNote = noteProject.snapshot();
  GRAPPLE_REQUIRE(afterCreateNote);
  const graph::GraphNode* createdNote = afterCreateNote.value().graph.findNode(foundation::NodeId{"node_note"});
  GRAPPLE_REQUIRE(createdNote != nullptr);
  GRAPPLE_REQUIRE(createdNote->kind == graph::NodeKind::Note);
  const auto* createdNotePayload = std::get_if<timeline::NotePayload>(&createdNote->payload);
  GRAPPLE_REQUIRE(createdNotePayload != nullptr);
  GRAPPLE_REQUIRE(createdNotePayload->title == "Framing note");
  GRAPPLE_REQUIRE(createdNotePayload->markdown == "Keep the subject editable.");
  const project::ProjectCommandEnvelope updateNote{
    foundation::CommandId{"cmd_update_note"},
    foundation::ProjectId{"proj_notes"},
    afterCreateNote.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateNoteCommand{
      foundation::NodeId{"node_note"},
      timeline::NotePayload{"Updated note", "Expose the control as a parameter."}
    }
  };
  GRAPPLE_REQUIRE(project::commandKind(updateNote.payload) == project::CommandKind::UpdateNote);
  GRAPPLE_REQUIRE(project::serializeCanonicalCommandPayload(updateNote.payload) == "{\"nodeId\":\"node_note\",\"title\":\"Updated note\",\"markdown\":\"Expose the control as a parameter.\"}");
  const auto updateNoteResult = noteProject.apply(updateNote);
  GRAPPLE_REQUIRE(updateNoteResult);
  GRAPPLE_REQUIRE(updateNoteResult.value().afterRevision == foundation::RevisionId{"rev_2"});
  const auto afterUpdateNote = noteProject.snapshot();
  GRAPPLE_REQUIRE(afterUpdateNote);
  const auto noteSnapshotRoundTrip = project::deserializeCanonicalProjectSnapshot(
    project::serializeCanonicalProjectSnapshot(afterUpdateNote.value())
  );
  GRAPPLE_REQUIRE(noteSnapshotRoundTrip);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalProjectSnapshot(noteSnapshotRoundTrip.value()) ==
    project::serializeCanonicalProjectSnapshot(afterUpdateNote.value())
  );
  const graph::GraphNode* updatedNote = noteSnapshotRoundTrip.value().graph.findNode(foundation::NodeId{"node_note"});
  GRAPPLE_REQUIRE(updatedNote != nullptr);
  const auto* updatedNotePayload = std::get_if<timeline::NotePayload>(&updatedNote->payload);
  GRAPPLE_REQUIRE(updatedNotePayload != nullptr);
  GRAPPLE_REQUIRE(updatedNotePayload->title == "Updated note");
  GRAPPLE_REQUIRE(updatedNotePayload->markdown == "Expose the control as a parameter.");
  const auto notesQuery = noteProject.query(project::ListNotesQuery{});
  GRAPPLE_REQUIRE(notesQuery);
  const auto* notesResult = std::get_if<project::NotesResult>(&notesQuery.value());
  GRAPPLE_REQUIRE(notesResult != nullptr);
  GRAPPLE_REQUIRE(notesResult->revision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(notesResult->notes.size() == 1);
  GRAPPLE_REQUIRE(notesResult->notes[0].nodeId == foundation::NodeId{"node_note"});
  GRAPPLE_REQUIRE(notesResult->notes[0].title == "Updated note");
  GRAPPLE_REQUIRE(notesResult->notes[0].markdown == "Expose the control as a parameter.");
  GRAPPLE_REQUIRE(notesResult->notes[0].enabled);
  const auto updateMissingNote = noteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_update_missing_note"},
    foundation::ProjectId{"proj_notes"},
    afterUpdateNote.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateNoteCommand{
      foundation::NodeId{"node_missing_note"},
      timeline::NotePayload{"Missing", "No node."}
    }
  });
  GRAPPLE_REQUIRE(!updateMissingNote);
  GRAPPLE_REQUIRE(updateMissingNote.error().code == "project.note_missing");
  const auto createNoteComposition = noteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_note_composition"},
    foundation::ProjectId{"proj_notes"},
    afterUpdateNote.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_note_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(createNoteComposition);
  const auto updateNonNote = noteProject.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_update_non_note"},
    foundation::ProjectId{"proj_notes"},
    createNoteComposition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::UpdateNoteCommand{
      foundation::NodeId{"node_note_composition"},
      timeline::NotePayload{"Wrong kind", "Composition is not a note."}
    }
  });
  GRAPPLE_REQUIRE(!updateNonNote);
  GRAPPLE_REQUIRE(updateNonNote.error().code == "project.note_missing");

  const auto graphQuery = controller.query(project::GetGraphQuery{});
  GRAPPLE_REQUIRE(graphQuery);
  const auto* graphResult = std::get_if<project::GraphResult>(&graphQuery.value());
  GRAPPLE_REQUIRE(graphResult != nullptr);
  GRAPPLE_REQUIRE(graphResult->revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(graphResult->graph.nodes().size() == 1);
  GRAPPLE_REQUIRE(graphResult->graph.edges().empty());

  const auto snapshotQuery = controller.query(project::GetProjectSnapshotQuery{});
  GRAPPLE_REQUIRE(snapshotQuery);
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&snapshotQuery.value());
  GRAPPLE_REQUIRE(snapshotResult != nullptr);
  GRAPPLE_REQUIRE(snapshotResult->snapshot.revision == foundation::RevisionId{"rev_3"});

  return 0;
}
