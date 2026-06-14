#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativeEffectSession.hpp>
#include <grapple/app/NativeMediaImport.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/history/HistorySerializer.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/render/LocalRenderCore.hpp>
#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/runtime/EffectRuntime.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>

#include <TestAssert.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace {

grapple::project::CommandSource userSource() {
  return grapple::project::CommandSource{
    grapple::project::CommandSourceKind::User,
    std::nullopt,
    "test"
  };
}

std::filesystem::path writeTinyPpm(const std::string& stem) {
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() /
    (stem + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ppm");
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output << "P6\n2 1\n255\n";
  const unsigned char pixels[] = {
    10, 20, 30,
    40, 50, 60
  };
  output.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
  return path;
}

void writeLittleEndianU16(std::ostream& output, std::uint16_t value) {
  const unsigned char bytes[] = {
    static_cast<unsigned char>(value & 0xffU),
    static_cast<unsigned char>((value >> 8U) & 0xffU)
  };
  output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeLittleEndianU32(std::ostream& output, std::uint32_t value) {
  const unsigned char bytes[] = {
    static_cast<unsigned char>(value & 0xffU),
    static_cast<unsigned char>((value >> 8U) & 0xffU),
    static_cast<unsigned char>((value >> 16U) & 0xffU),
    static_cast<unsigned char>((value >> 24U) & 0xffU)
  };
  output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

std::filesystem::path writeTinyWav(const std::string& stem) {
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() /
    (stem + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
  constexpr std::uint16_t channelCount = 1;
  constexpr std::uint32_t sampleRate = 8000;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint32_t frameCount = 8000;
  constexpr std::uint32_t dataBytes = frameCount * channelCount * (bitsPerSample / 8U);

  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output.write("RIFF", 4);
  writeLittleEndianU32(output, 36U + dataBytes);
  output.write("WAVE", 4);
  output.write("fmt ", 4);
  writeLittleEndianU32(output, 16);
  writeLittleEndianU16(output, 1);
  writeLittleEndianU16(output, channelCount);
  writeLittleEndianU32(output, sampleRate);
  writeLittleEndianU32(output, sampleRate * channelCount * (bitsPerSample / 8U));
  writeLittleEndianU16(output, channelCount * (bitsPerSample / 8U));
  writeLittleEndianU16(output, bitsPerSample);
  output.write("data", 4);
  writeLittleEndianU32(output, dataBytes);
  std::string samples(dataBytes, '\0');
  output.write(samples.data(), static_cast<std::streamsize>(samples.size()));
  return path;
}

class CountingCameraTransformRuntime final : public grapple::runtime::IEffectRuntime {
public:
  bool supports(const grapple::projection::RenderEffectNode& node) const override {
    return node.payload.implementation.kind == grapple::timeline::EffectImplementationKind::Builtin &&
           node.payload.implementation.entrypoint == grapple::runtime::builtin_effect::CameraTransformEntrypoint;
  }

  grapple::foundation::Result<grapple::runtime::EffectPrepareResult> prepare(
    const grapple::runtime::EffectPrepareRequest& request
  ) override {
    ++prepareCount;
    return grapple::runtime::EffectPrepareResult{
      grapple::runtime::PreparedEffectNode{
        request.graph.id,
        request.graph.targetNodeId,
        request.node.sourceNodeId,
        request.node.payload.activeRange,
        nullptr,
        grapple::runtime::RuntimeParamSet{},
        {}
      },
      {}
    };
  }

  grapple::foundation::Result<grapple::runtime::EffectProcessResult> process(
    const grapple::runtime::EffectProcessRequest& request
  ) override {
    return grapple::runtime::EffectProcessResult{
      grapple::runtime::RuntimeEffectOutput{
        request.prepared.effectGraphId,
        request.prepared.targetNodeId,
        request.prepared.sourceNodeId,
        {}
      },
      {}
    };
  }

  int prepareCount = 0;
};

} // namespace

int main() {
  using namespace grapple;

  const std::filesystem::path appPackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_primary_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  app::NativeProjectSession session{
    foundation::ProjectId{"proj_app"},
    "App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app"},
      foundation::FilePath{appPackageRoot.string()},
      1
    }
  };

  const std::filesystem::path packageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path stewardPackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_steward_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path projectOnlyPackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_project_only_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path cachePackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_cache_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  app::NativeProjectSession savedSession{
    foundation::ProjectId{"proj_app_saved"},
    "Saved App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_saved"},
      foundation::FilePath{packageRoot.string()},
      1
    }
  };

  const auto initial = session.snapshot();
  GRAPPLE_REQUIRE(initial);

  const std::filesystem::path inspectedImagePath = writeTinyPpm("grapple_native_inspect_image");
  const auto inspectedImage = app::inspectNativeMediaAsset(
    foundation::AssetId{"asset_inspected_image"},
    foundation::FilePath{inspectedImagePath.string()}
  );
  GRAPPLE_REQUIRE(inspectedImage);
  GRAPPLE_REQUIRE(inspectedImage.value().id == foundation::AssetId{"asset_inspected_image"});
  GRAPPLE_REQUIRE(inspectedImage.value().metadata.mediaType == asset::AssetMediaType::Image);
  GRAPPLE_REQUIRE(inspectedImage.value().metadata.sourcePath == foundation::FilePath{inspectedImagePath.string()});
  GRAPPLE_REQUIRE(!inspectedImage.value().metadata.duration.has_value());
  GRAPPLE_REQUIRE(inspectedImage.value().metadata.dimensions.has_value());
  GRAPPLE_REQUIRE((inspectedImage.value().metadata.dimensions.value() == foundation::Resolution{2, 1}));

  const std::filesystem::path inspectedAudioPath = writeTinyWav("grapple_native_inspect_audio");
  const auto inspectedAudio = app::inspectNativeMediaAsset(
    foundation::AssetId{"asset_inspected_audio"},
    foundation::FilePath{inspectedAudioPath.string()}
  );
  GRAPPLE_REQUIRE(inspectedAudio);
  GRAPPLE_REQUIRE(inspectedAudio.value().id == foundation::AssetId{"asset_inspected_audio"});
  GRAPPLE_REQUIRE(inspectedAudio.value().metadata.mediaType == asset::AssetMediaType::Audio);
  GRAPPLE_REQUIRE(inspectedAudio.value().metadata.sourcePath == foundation::FilePath{inspectedAudioPath.string()});
  GRAPPLE_REQUIRE(inspectedAudio.value().metadata.duration.has_value());
  GRAPPLE_REQUIRE(inspectedAudio.value().metadata.duration.value() == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(!inspectedAudio.value().metadata.dimensions.has_value());

  std::filesystem::remove(inspectedImagePath);
  std::filesystem::remove(inspectedAudioPath);

  app::NativeProjectCommandWriter writer{session};
  const foundation::NodeId compositionNodeId = writer.nextNodeId("composition");
  const auto composition = writer.apply(
    project::CreateCompositionCommand{compositionNodeId, "Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(composition);
  GRAPPLE_REQUIRE(composition.value().snapshot.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(composition.value().commandResult.commandId == foundation::CommandId{"cmd_app_1"});
  GRAPPLE_REQUIRE(compositionNodeId == foundation::NodeId{"node_composition_1"});
  GRAPPLE_REQUIRE(writer.nextAssetId("walking woman") == foundation::AssetId{"asset_walking_woman_1"});
  GRAPPLE_REQUIRE(writer.nextEdgeId("contains track") == foundation::EdgeId{"edge_contains_track_1"});
  GRAPPLE_REQUIRE(writer.nextSnapshotId("rev 1") == foundation::SnapshotId{"snap_rev_1_2"});
  bool rejectedEmptyIdStem = false;
  try {
    (void)writer.nextNodeId("...");
  } catch (const std::invalid_argument&) {
    rejectedEmptyIdStem = true;
  }
  GRAPPLE_REQUIRE(rejectedEmptyIdStem);
  GRAPPLE_REQUIRE(session.packageState().head.has_value());
  GRAPPLE_REQUIRE(session.packageState().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().head->lastSnapshotId == foundation::SnapshotId{"snap_cmd_app_1_1"});
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(session.packageState().snapshots.records().size() == 1);

  app::NativeProjectSession commandServiceSession{
    foundation::ProjectId{"proj_app_command_service"},
    "Command Service App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_command_service"},
      foundation::FilePath{"command-service-app.grapple"},
      1
    }
  };
  app::NativeProjectCommandWriter commandServiceWriter{commandServiceSession};
  project::IProjectCommandService& commandService = commandServiceWriter;
  const auto commandServiceResult = commandService.apply(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_agent_create_composition"},
    foundation::ProjectId{"proj_app_command_service"},
    foundation::RevisionId{"rev_0"},
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_agent_service"}, "agent"},
    project::CreateCompositionCommand{foundation::NodeId{"node_service_composition"}, "Service Main"}
  });
  GRAPPLE_REQUIRE(commandServiceResult);
  GRAPPLE_REQUIRE(commandServiceResult.value().commandId == foundation::CommandId{"cmd_agent_create_composition"});
  GRAPPLE_REQUIRE(commandServiceResult.value().afterRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(commandServiceSession.packageState().head.has_value());
  GRAPPLE_REQUIRE(commandServiceSession.packageState().head->lastSnapshotId == foundation::SnapshotId{"snap_cmd_agent_create_composition_1"});
  GRAPPLE_REQUIRE(commandServiceSession.packageState().snapshots.records().size() == 1);

  const auto beforeReadQueries = session.snapshot();
  GRAPPLE_REQUIRE(beforeReadQueries);
  const std::size_t commandCountBeforeReadQueries = session.packageState().commandLog.records().size();
  const std::size_t snapshotCountBeforeReadQueries = session.packageState().snapshots.records().size();
  const auto snapshotQuery = session.query(project::GetProjectSnapshotQuery{});
  GRAPPLE_REQUIRE(snapshotQuery);
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&snapshotQuery.value());
  GRAPPLE_REQUIRE(snapshotResult != nullptr);
  GRAPPLE_REQUIRE(snapshotResult->snapshot.revision == foundation::RevisionId{"rev_1"});

  const auto graphQuery = session.query(project::GetGraphQuery{});
  GRAPPLE_REQUIRE(graphQuery);
  const auto* graphResult = std::get_if<project::GraphResult>(&graphQuery.value());
  GRAPPLE_REQUIRE(graphResult != nullptr);
  GRAPPLE_REQUIRE(graphResult->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(graphResult->graph.nodes().size() == 1);

  const auto assetCatalogQuery = session.query(project::GetAssetCatalogQuery{});
  GRAPPLE_REQUIRE(assetCatalogQuery);
  const auto* assetCatalogResult = std::get_if<project::AssetCatalogResult>(&assetCatalogQuery.value());
  GRAPPLE_REQUIRE(assetCatalogResult != nullptr);
  GRAPPLE_REQUIRE(assetCatalogResult->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(assetCatalogResult->assets.assets().empty());

  const auto timeline = session.buildTimelineIR();
  GRAPPLE_REQUIRE(timeline);
  GRAPPLE_REQUIRE(timeline.value().timeline.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(timeline.value().timeline.layers.empty());

  const auto renderPlan = session.buildRenderPlan();
  GRAPPLE_REQUIRE(renderPlan);
  GRAPPLE_REQUIRE(renderPlan.value().plan.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(renderPlan.value().plan.effectGraphs.empty());

  const auto renderPlanQuery = session.query(project::InspectRenderPlanQuery{});
  GRAPPLE_REQUIRE(renderPlanQuery);
  const auto* renderPlanInspectResult = std::get_if<project::RenderPlanInspectResult>(&renderPlanQuery.value());
  GRAPPLE_REQUIRE(renderPlanInspectResult != nullptr);
  GRAPPLE_REQUIRE(renderPlanInspectResult->projectId == foundation::ProjectId{"proj_app"});
  GRAPPLE_REQUIRE(renderPlanInspectResult->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(renderPlanInspectResult->duration == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(renderPlanInspectResult->assetCount == 0);
  GRAPPLE_REQUIRE(renderPlanInspectResult->layers.empty());
  GRAPPLE_REQUIRE(renderPlanInspectResult->clips.empty());
  GRAPPLE_REQUIRE(renderPlanInspectResult->cameras.empty());
  GRAPPLE_REQUIRE(renderPlanInspectResult->effectGraphs.empty());
  GRAPPLE_REQUIRE(renderPlanInspectResult->diagnosticCount == 0);
  const auto afterReadQueries = session.snapshot();
  GRAPPLE_REQUIRE(afterReadQueries);
  GRAPPLE_REQUIRE(afterReadQueries.value().revision == beforeReadQueries.value().revision);
  GRAPPLE_REQUIRE(afterReadQueries.value().canonicalHash == beforeReadQueries.value().canonicalHash);
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == commandCountBeforeReadQueries);
  GRAPPLE_REQUIRE(session.packageState().snapshots.records().size() == snapshotCountBeforeReadQueries);

  const auto viewModel = session.buildViewModel();
  GRAPPLE_REQUIRE(viewModel);
  GRAPPLE_REQUIRE(viewModel.value().project.projectId == foundation::ProjectId{"proj_app"});
  GRAPPLE_REQUIRE(viewModel.value().project.name == "App Project");
  GRAPPLE_REQUIRE(viewModel.value().project.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(viewModel.value().project.revisionNumber == 1);
  GRAPPLE_REQUIRE(viewModel.value().assets.count == 0);
  GRAPPLE_REQUIRE(viewModel.value().assets.rows.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.duration == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(viewModel.value().timeline.compositions.size() == 1);
  GRAPPLE_REQUIRE(viewModel.value().timeline.compositions[0].sourceNodeId == foundation::NodeId{"node_composition_1"});
  GRAPPLE_REQUIRE(viewModel.value().timeline.compositions[0].name == "Main");
  GRAPPLE_REQUIRE(viewModel.value().timeline.layers.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.clips.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.cameras.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.effectGraphs.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.effectCount == 0);

  const auto manifest = storage::buildProjectPackageManifest(session.packageState());
  GRAPPLE_REQUIRE(manifest);
  GRAPPLE_REQUIRE(manifest.value().head.has_value());
  GRAPPLE_REQUIRE(manifest.value().head->lastCommandId == foundation::CommandId{"cmd_app_1"});
  GRAPPLE_REQUIRE(manifest.value().snapshots.size() == 1);
  GRAPPLE_REQUIRE(manifest.value().snapshots[0].id == foundation::SnapshotId{"snap_cmd_app_1_1"});
  GRAPPLE_REQUIRE(manifest.value().snapshots[0].revision == foundation::RevisionId{"rev_1"});

  const auto writeCurrentSnapshot = session.writePackage();
  GRAPPLE_REQUIRE(writeCurrentSnapshot);
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().snapshotPath.value == (appPackageRoot / "snapshots/snap_cmd_app_1_1.json").lexically_normal().string());
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().manifestPath.value == (appPackageRoot / "manifest.json").lexically_normal().string());
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().commandLogPath.value == (appPackageRoot / "history/commands.json").lexically_normal().string());
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().eventLogPath.value == (appPackageRoot / "history/events.json").lexically_normal().string());
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().schemaMigrationLogPath.value == (appPackageRoot / "history/schema_migrations.json").lexically_normal().string());

  app::NativeProjectSession restoreSession{
    foundation::ProjectId{"proj_app_restore"},
    "Restore App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_restore"},
      foundation::FilePath{"restore-app.grapple"},
      1
    }
  };
  app::NativeProjectCommandWriter restoreWriter{restoreSession};
  const foundation::NodeId restoreCompositionNodeId = restoreWriter.nextNodeId("composition");
  const auto restoreComposition = restoreWriter.apply(
    project::CreateCompositionCommand{restoreCompositionNodeId, "Restore Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(restoreComposition);
  const foundation::NodeId restoreTrackNodeId = restoreWriter.nextNodeId("track");
  const auto restoreTrack = restoreWriter.apply(
    project::CreateTrackCommand{
      restoreTrackNodeId,
      restoreCompositionNodeId,
      restoreWriter.nextEdgeId("contains track"),
      "Temporary Track",
      timeline::TrackKind::Visual
    },
    userSource()
  );
  GRAPPLE_REQUIRE(restoreTrack);
  GRAPPLE_REQUIRE(restoreSession.packageState().snapshotDocuments.size() == 2);
  const auto undoRevision = restoreWriter.undoLastCommittedCommand(
    userSource(),
    std::optional<std::string>{"undo track creation"}
  );
  GRAPPLE_REQUIRE(undoRevision);
  GRAPPLE_REQUIRE(undoRevision.value().commandResult.beforeRevision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(undoRevision.value().commandResult.afterRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(undoRevision.value().snapshot.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(undoRevision.value().snapshot.graph.nodes().size() == 1);
  GRAPPLE_REQUIRE(undoRevision.value().snapshot.graph.edges().empty());
  GRAPPLE_REQUIRE(restoreSession.packageState().commandLog.records().size() == 3);
  GRAPPLE_REQUIRE(restoreSession.packageState().commandLog.records().back().serializedName == "project.restore_snapshot");
  GRAPPLE_REQUIRE(restoreSession.packageState().snapshots.records().size() == 3);
  GRAPPLE_REQUIRE(restoreSession.packageState().snapshots.records().back().label == std::optional<std::string>{"undo track creation"});
  GRAPPLE_REQUIRE(restoreSession.packageState().snapshotDocuments.size() == 3);
  GRAPPLE_REQUIRE(restoreSession.packageState().snapshotDocuments.back().revision == foundation::RevisionId{"rev_3"});
  const auto redoneRevision = restoreWriter.redoLastUndoneCommand(
    userSource(),
    std::optional<std::string>{"redo track creation"}
  );
  GRAPPLE_REQUIRE(redoneRevision);
  GRAPPLE_REQUIRE(redoneRevision.value().commandResult.beforeRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(redoneRevision.value().commandResult.afterRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(redoneRevision.value().snapshot.graph.nodes().size() == 2);
  GRAPPLE_REQUIRE(redoneRevision.value().snapshot.graph.edges().size() == 1);
  GRAPPLE_REQUIRE(restoreSession.packageState().commandLog.records().size() == 4);
  GRAPPLE_REQUIRE(restoreSession.packageState().commandLog.records().back().serializedName == "project.create_track");
  GRAPPLE_REQUIRE(restoreSession.packageState().snapshotDocuments.size() == 4);

  app::NativeProjectSession assetSession{
    foundation::ProjectId{"proj_app_assets"},
    "Asset App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_assets"},
      foundation::FilePath{"asset-app.grapple"},
      1
    }
  };
  app::NativeProjectCommandWriter assetWriter{assetSession};
  const auto registeredAsset = assetWriter.apply(
    project::RegisterAssetCommand{asset::Asset{
      foundation::AssetId{"asset_clip"},
      "Clip",
      asset::AssetMetadata{
        asset::AssetMediaType::Video,
        foundation::FilePath{"/tmp/clip.mov"},
        std::nullopt,
        foundation::TimeSeconds{12.5},
        foundation::Resolution{1920, 1080},
        foundation::FrameRate{30000, 1001}
      }
    }},
    userSource()
  );
  GRAPPLE_REQUIRE(registeredAsset);
  const auto assetViewModel = assetSession.buildViewModel();
  GRAPPLE_REQUIRE(assetViewModel);
  GRAPPLE_REQUIRE(assetViewModel.value().assets.count == 1);
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows.size() == 1);
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].assetId == foundation::AssetId{"asset_clip"});
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].name == "Clip");
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].mediaType == "video");
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].sourcePath == foundation::FilePath{"/tmp/clip.mov"});
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].duration == foundation::TimeSeconds{12.5});
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].dimensions.has_value());
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].dimensions->width == 1920);
  GRAPPLE_REQUIRE(assetViewModel.value().assets.rows[0].dimensions->height == 1080);
  const foundation::NodeId assetCompositionNodeId = assetWriter.nextNodeId("composition");
  const auto assetComposition = assetWriter.apply(
    project::CreateCompositionCommand{assetCompositionNodeId, "Asset Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(assetComposition);
  const foundation::NodeId assetTrackNodeId = assetWriter.nextNodeId("track");
  const auto assetTrack = assetWriter.apply(
    project::CreateTrackCommand{
      assetTrackNodeId,
      assetCompositionNodeId,
      assetWriter.nextEdgeId("contains track"),
      "Asset Track",
      timeline::TrackKind::Visual
    },
    userSource()
  );
  GRAPPLE_REQUIRE(assetTrack);
  const timeline::Transform clipTransform{
    foundation::Vec2{0.2, -0.3},
    foundation::Vec2{1.4, 0.8},
    12.0,
    0.75
  };
  const foundation::NodeId assetClipNodeId = assetWriter.nextNodeId("clip");
  const auto assetClip = assetWriter.apply(
    project::CreateClipCommand{
      assetClipNodeId,
      assetTrackNodeId,
      assetWriter.nextEdgeId("contains clip"),
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}},
        1.0,
        foundation::AssetId{"asset_clip"},
        clipTransform
      },
      0
    },
    userSource()
  );
  GRAPPLE_REQUIRE(assetClip);
  const auto assetClipViewModel = assetSession.buildViewModel();
  GRAPPLE_REQUIRE(assetClipViewModel);
  GRAPPLE_REQUIRE(assetClipViewModel.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE(assetClipViewModel.value().timeline.clips[0].sourceNodeId == assetClipNodeId);
  GRAPPLE_REQUIRE(assetClipViewModel.value().timeline.clips[0].assetName == "Clip");
  GRAPPLE_REQUIRE(assetClipViewModel.value().timeline.clips[0].transform == clipTransform);

  const std::filesystem::path cacheImagePath = writeTinyPpm("grapple_native_cache_image");
  app::NativeProjectSession cacheProject{
    foundation::ProjectId{"proj_app_cache"},
    "Cache App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_cache"},
      foundation::FilePath{cachePackageRoot.string()},
      1
    }
  };
  app::NativeProjectCommandWriter cacheWriter{cacheProject};
  const auto cacheAsset = cacheWriter.apply(
    project::RegisterAssetCommand{asset::Asset{
      foundation::AssetId{"asset_cache_image"},
      "Cache Image",
      asset::AssetMetadata{
        asset::AssetMediaType::Image,
        foundation::FilePath{cacheImagePath.string()},
        std::nullopt,
        foundation::TimeSeconds{1.0},
        foundation::Resolution{2, 1},
        std::nullopt
      }
    }},
    userSource()
  );
  GRAPPLE_REQUIRE(cacheAsset);
  const auto cacheAudioAsset = cacheWriter.apply(
    project::RegisterAssetCommand{asset::Asset{
      foundation::AssetId{"asset_cache_audio"},
      "Cache Audio",
      asset::AssetMetadata{
        asset::AssetMediaType::Audio,
        foundation::FilePath{"/tmp/grapple-cache-audio.wav"},
        std::nullopt,
        foundation::TimeSeconds{1.0},
        std::nullopt,
        std::nullopt
      }
    }},
    userSource()
  );
  GRAPPLE_REQUIRE(cacheAudioAsset);
  const foundation::NodeId cacheCompositionNodeId = cacheWriter.nextNodeId("composition");
  const auto cacheComposition = cacheWriter.apply(
    project::CreateCompositionCommand{cacheCompositionNodeId, "Cache Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(cacheComposition);
  const foundation::NodeId cacheTrackNodeId = cacheWriter.nextNodeId("track");
  const auto cacheTrack = cacheWriter.apply(
    project::CreateTrackCommand{
      cacheTrackNodeId,
      cacheCompositionNodeId,
      cacheWriter.nextEdgeId("contains track"),
      "Cache Track",
      timeline::TrackKind::Visual
    },
    userSource()
  );
  GRAPPLE_REQUIRE(cacheTrack);
  const auto cacheClip = cacheWriter.apply(
    project::CreateClipCommand{
      cacheWriter.nextNodeId("clip"),
      cacheTrackNodeId,
      cacheWriter.nextEdgeId("contains clip"),
      timeline::ClipPayload{
        timeline::ClipKind::Image,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
        1.0,
        foundation::AssetId{"asset_cache_image"},
        timeline::Transform{}
      },
      0
    },
    userSource()
  );
  GRAPPLE_REQUIRE(cacheClip);
  auto cacheWorkspace = app::NativeWorkspaceSession::fromProject(std::move(cacheProject));
  GRAPPLE_REQUIRE(cacheWorkspace);
  GRAPPLE_REQUIRE(cacheWorkspace.value().mediaSources().sources().size() == 2);
  const media::MediaSource* cacheAudioSource =
    cacheWorkspace.value().mediaSources().find(foundation::AssetId{"asset_cache_audio"});
  GRAPPLE_REQUIRE(cacheAudioSource != nullptr);
  GRAPPLE_REQUIRE(cacheAudioSource->kind == media::MediaSourceKind::Audio);
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 0);
  const auto exportOnlyPrepare = cacheWorkspace.value().exportSession().prepareFromProject();
  GRAPPLE_REQUIRE(exportOnlyPrepare);
  const auto exportOnlyResult = cacheWorkspace.value().exportSession().render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{1, 1},
    foundation::Resolution{2, 1},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/cache-export.mov"}
  });
  GRAPPLE_REQUIRE(exportOnlyResult);
  GRAPPLE_REQUIRE(exportOnlyResult.value().framesEvaluated == 1);
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 1);
  const std::filesystem::path videoExportPath =
    std::filesystem::temp_directory_path() /
    ("grapple_native_video_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".avi");
  const auto videoExport = cacheWorkspace.value().exportSession().renderToVideo(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{1, 1},
    foundation::Resolution{16, 16},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{videoExportPath.string()}
  });
  GRAPPLE_REQUIRE(videoExport);
  GRAPPLE_REQUIRE(videoExport.value().framesEvaluated == 1);
  GRAPPLE_REQUIRE(std::filesystem::exists(videoExportPath));
  GRAPPLE_REQUIRE(std::filesystem::file_size(videoExportPath) > 0);
  std::filesystem::remove(videoExportPath);
  const auto cacheRefresh = cacheWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(cacheRefresh);
  const auto firstCachedFrame = cacheWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(firstCachedFrame);
  GRAPPLE_REQUIRE(firstCachedFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 2);
  const auto secondCachedFrame = cacheWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(secondCachedFrame);
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 2);
  const auto cacheWorkspaceWrite = cacheWorkspace.value().writePackage();
  GRAPPLE_REQUIRE(cacheWorkspaceWrite);
  auto reopenedCacheWorkspace = app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{cachePackageRoot.string()});
  GRAPPLE_REQUIRE(reopenedCacheWorkspace);
  GRAPPLE_REQUIRE(reopenedCacheWorkspace.value().mediaSources().sources().size() == 2);
  const media::MediaSource* reopenedCacheImageSource =
    reopenedCacheWorkspace.value().mediaSources().find(foundation::AssetId{"asset_cache_image"});
  GRAPPLE_REQUIRE(reopenedCacheImageSource != nullptr);
  GRAPPLE_REQUIRE(reopenedCacheImageSource->kind == media::MediaSourceKind::Image);
  const media::MediaSource* reopenedCacheAudioSource =
    reopenedCacheWorkspace.value().mediaSources().find(foundation::AssetId{"asset_cache_audio"});
  GRAPPLE_REQUIRE(reopenedCacheAudioSource != nullptr);
  GRAPPLE_REQUIRE(reopenedCacheAudioSource->kind == media::MediaSourceKind::Audio);
  const auto reopenedCacheRefresh = reopenedCacheWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(reopenedCacheRefresh);
  const auto reopenedCacheFrame = reopenedCacheWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(reopenedCacheFrame);
  GRAPPLE_REQUIRE(reopenedCacheFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE((reopenedCacheFrame.value().frame.image->resolution == foundation::Resolution{2, 1}));
  std::filesystem::remove(cacheImagePath);
  std::filesystem::remove_all(cachePackageRoot);

  app::NativeProjectSession effectSession{
    foundation::ProjectId{"proj_app_effects"},
    "Effect App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_effects"},
      foundation::FilePath{"effect-app.grapple"},
      1
    }
  };
  app::NativeProjectCommandWriter effectWriter{effectSession};
  const foundation::NodeId effectCompositionNodeId = effectWriter.nextNodeId("composition");
  const auto effectComposition = effectWriter.apply(
    project::CreateCompositionCommand{effectCompositionNodeId, "Effects Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(effectComposition);
  const foundation::NodeId effectCameraNodeId = effectWriter.nextNodeId("camera");
  const timeline::Transform effectCameraTransform{
    foundation::Vec2{0.4, 0.1},
    foundation::Vec2{1.2, 1.1},
    -5.0,
    1.0
  };
  const auto effectCamera = effectWriter.apply(
    project::CreateCameraCommand{
      effectCameraNodeId,
      effectCompositionNodeId,
      effectWriter.nextEdgeId("contains camera"),
      timeline::CameraPayload{"Camera", effectCameraTransform, timeline::CameraLens{35.0}}
    },
    userSource()
  );
  GRAPPLE_REQUIRE(effectCamera);
  const foundation::NodeId effectNodeId = effectWriter.nextNodeId("effect");
  const auto effectCommand = effectWriter.apply(
    project::CreateEffectCommand{
      effectNodeId,
      effectCameraNodeId,
      effectWriter.nextEdgeId("effect targets camera"),
      timeline::EffectPayload{
        "Camera Follow",
        timeline::EffectImplementation{
          timeline::EffectImplementationKind::Python,
          "prepare",
          timeline::EffectSource{
            timeline::EffectSourceKind::InlineSource,
            "python",
            "def prepare(ctx): return {}\n",
            std::nullopt,
            foundation::stableHash("def prepare(ctx): return {}\n")
          }
        },
        timeline::EffectPortSet{
          {timeline::EffectPort{"frame"}},
          {timeline::EffectPort{"camera"}}
        },
        timeline::ParamSet{
          {timeline::Param{
            "target_x",
            0.5,
            timeline::Param::Control{
              "Target X",
              timeline::Param::NumericControl{0.0, 1.0, 0.01}
            }
          },
          timeline::Param{
            "lock_subject",
            true,
            timeline::Param::Control{
              "Lock Subject",
              std::nullopt
            }
          }}
        },
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}}
      },
      graph::PortName{"camera"},
      graph::PortName{"input"},
      0
    },
    userSource()
  );
  GRAPPLE_REQUIRE(effectCommand);
  const auto effectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(effectViewModel);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.cameras.size() == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.cameras[0].sourceNodeId == effectCameraNodeId);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.cameras[0].transform == effectCameraTransform);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectCount == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].targetNodeId == effectCameraNodeId);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].nodeCount == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].edgeCount == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].targetNodeId == effectCameraNodeId);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].displayName == "Camera Follow");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].implementationKind == "python");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].entrypoint == "prepare");
  GRAPPLE_REQUIRE(!effectViewModel.value().timeline.effectGraphs[0].effects[0].cameraTransformEffect);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params.size() == 2);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].name == "target_x");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].label == "Target X");
  GRAPPLE_REQUIRE(std::get<double>(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.5);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].numericMin == 0.0);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].numericMax == 1.0);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].numericStep == 0.01);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.empty());
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].name == "lock_subject");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].label == "Lock Subject");
  GRAPPLE_REQUIRE(std::get<bool>(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value));
  GRAPPLE_REQUIRE(app::paramValueDisplayText(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value) == "true");
  app::NativeEffectSession effectEdits{effectSession, effectWriter};
  const auto appKeyframeUpsert = effectEdits.upsertParamKeyframe(
    effectNodeId,
    "target_x",
    timeline::Param::Keyframe{
      foundation::KeyframeId{"key_target_x_2"},
      foundation::TimeSeconds{1.25},
      0.8
    },
    userSource()
  );
  GRAPPLE_REQUIRE(appKeyframeUpsert);
  const auto keyframedEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(keyframedEffectViewModel);
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].keyframeId == foundation::KeyframeId{"key_target_x_2"});
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].time == foundation::TimeSeconds{1.25});
  GRAPPLE_REQUIRE(std::get<double>(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].value) == 0.8);
  app::NativeProjectCommandWriter sparseKeyframeWriter{effectSession};
  GRAPPLE_REQUIRE(sparseKeyframeWriter.nextKeyframeId("target x") == foundation::KeyframeId{"key_target_x_3"});
  const auto appParamValueUpdate = effectEdits.setParamValue(
    effectNodeId,
    "target_x",
    0.6,
    userSource()
  );
  GRAPPLE_REQUIRE(appParamValueUpdate);
  const auto valueUpdatedEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel);
  GRAPPLE_REQUIRE(std::get<double>(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.6);
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].keyframeId == foundation::KeyframeId{"key_target_x_2"});
  const auto boolParamUpdate = effectEdits.setParamValue(
    effectNodeId,
    "lock_subject",
    false,
    userSource()
  );
  GRAPPLE_REQUIRE(boolParamUpdate);
  const auto boolUpdatedEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(boolUpdatedEffectViewModel);
  GRAPPLE_REQUIRE(!std::get<bool>(boolUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value));
  const auto appKeyframeDelete = effectEdits.deleteParamKeyframe(
    effectNodeId,
    "target_x",
    foundation::KeyframeId{"key_target_x_2"},
    userSource()
  );
  GRAPPLE_REQUIRE(appKeyframeDelete);
  const auto unkeyframedEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(unkeyframedEffectViewModel);
  GRAPPLE_REQUIRE(unkeyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.empty());
  const auto secondEffectCommand = effectWriter.apply(
    project::CreateEffectCommand{
      foundation::NodeId{"node_second_effect"},
      effectCameraNodeId,
      foundation::EdgeId{"edge_second_effect_targets_camera"},
      timeline::EffectPayload{
        "Camera Ease",
        timeline::EffectImplementation{
          timeline::EffectImplementationKind::Python,
          "prepare",
          timeline::EffectSource{
            timeline::EffectSourceKind::InlineSource,
            "python",
            "def prepare(ctx): return {'ease': True}\n",
            std::nullopt,
            foundation::stableHash("def prepare(ctx): return {'ease': True}\n")
          }
        },
        timeline::EffectPortSet{
          {timeline::EffectPort{"frame"}},
          {timeline::EffectPort{"camera"}}
        },
        timeline::ParamSet{
          {timeline::Param{
            "smoothness",
            0.25,
            timeline::Param::Control{
              "Smoothness",
              timeline::Param::NumericControl{0.0, 1.0, 0.01}
            }
          }}
        },
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{10.0}}
      },
      graph::PortName{"camera"},
      graph::PortName{"input"},
      1
    },
    userSource()
  );
  GRAPPLE_REQUIRE(secondEffectCommand);
  const auto twoEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(twoEffectViewModel);
  GRAPPLE_REQUIRE(twoEffectViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(twoEffectViewModel.value().timeline.effectGraphs[0].effects.size() == 2);
  GRAPPLE_REQUIRE(twoEffectViewModel.value().timeline.effectCount == 2);

  app::NativeProjectSession runtimeProject{
    foundation::ProjectId{"proj_app_runtime"},
    "Runtime App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_runtime"},
      foundation::FilePath{"runtime-app.grapple"},
      1
    }
  };
  auto runtimeWorkspace = app::NativeWorkspaceSession::fromProject(std::move(runtimeProject));
  GRAPPLE_REQUIRE(runtimeWorkspace);
  const foundation::NodeId runtimeCompositionNodeId = runtimeWorkspace.value().commandWriter().nextNodeId("composition");
  const auto runtimeComposition = runtimeWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{runtimeCompositionNodeId, "Runtime Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(runtimeComposition);
  const foundation::NodeId runtimeCameraNodeId = runtimeWorkspace.value().commandWriter().nextNodeId("camera");
  const auto runtimeCamera = runtimeWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      runtimeCameraNodeId,
      runtimeCompositionNodeId,
      runtimeWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{"Camera", timeline::Transform{}, timeline::CameraLens{35.0}}
    },
    userSource()
  );
  GRAPPLE_REQUIRE(runtimeCamera);
  GRAPPLE_REQUIRE(runtimeWorkspace.value().steward().conversationState().runs.empty());
  const auto runtimeEffect = runtimeWorkspace.value().steward().createCameraTransformEffect(
    runtimeCameraNodeId,
    "Center the subject with an editable camera transform.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
  );
  GRAPPLE_REQUIRE(runtimeEffect);
  GRAPPLE_REQUIRE(runtimeWorkspace.value().project().packageState().snapshots.records().back().label.has_value());
  GRAPPLE_REQUIRE(runtimeWorkspace.value().project().packageState().snapshots.records().back().label.value() == "Center the subject with an editable camera transform.");
  const auto duplicateRuntimeEffect = runtimeWorkspace.value().steward().createCameraTransformEffect(
    runtimeCameraNodeId,
    "Center the subject with an editable camera transform.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
  );
  GRAPPLE_REQUIRE(!duplicateRuntimeEffect);
  GRAPPLE_REQUIRE(duplicateRuntimeEffect.error().code == "steward.camera_transform_exists");
  const agent::AgentConversationState stewardConversation = runtimeWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].title == "Create editable camera transform");
  GRAPPLE_REQUIRE(stewardConversation.runs[0].messages.size() == 1);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls[0].toolSerializedId == "steward.create_camera_transform");
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls[0].status == agent::AgentConversationToolCallStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].status == agent::AgentRunStatus::Failed);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].toolCalls[0].status == agent::AgentConversationToolCallStatus::Failed);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].diagnostics.size() == 1);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].diagnostics[0].code == "steward.camera_transform_exists");
  GRAPPLE_REQUIRE(runtimeWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.has_value());
  GRAPPLE_REQUIRE(runtimeWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.value() == stewardConversation.runs[0].runId);
  const auto runtimeEffectViewModel = runtimeWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(runtimeEffectViewModel);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].displayName == "Camera Transform");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].implementationKind == "builtin");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].cameraTransformEffect);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params.size() == 3);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].label == "Position X");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].label == "Position Y");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].label == "Zoom");
  GRAPPLE_REQUIRE(std::get<double>(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.15);
  GRAPPLE_REQUIRE(std::get<double>(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].value) == 1.1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].numericMin == 0.25);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].numericMax == 4.0);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].numericStep == 0.01);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].intent == "Center the subject with an editable camera transform.");
  const foundation::NodeId runtimeEffectNodeId = runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].sourceNodeId;
  const auto initialRuntimeRefresh = runtimeWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(initialRuntimeRefresh);
  const auto initialRuntimeFrame = runtimeWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(initialRuntimeFrame);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras.size() == 1);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].cameraNodeId == runtimeCameraNodeId);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].transform.position.x == 0.15);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].transform.position.y == 0.0);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].transform.scale.x == 1.1);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].transform.scale.y == 1.1);
  const auto updatedRuntimeEffect = runtimeWorkspace.value().effects().setParamValue(
    runtimeEffectNodeId,
    runtime::builtin_effect::PositionXParam,
    0.25,
    userSource()
  );
  GRAPPLE_REQUIRE(updatedRuntimeEffect);
  const auto updatedRuntimeZoom = runtimeWorkspace.value().effects().setParamValue(
    runtimeEffectNodeId,
    runtime::builtin_effect::ZoomParam,
    1.5,
    userSource()
  );
  GRAPPLE_REQUIRE(updatedRuntimeZoom);
  const auto updatedRuntimeEffectViewModel = runtimeWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel);
  GRAPPLE_REQUIRE(std::get<double>(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.25);
  GRAPPLE_REQUIRE(std::get<double>(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].value) == 1.5);
  const auto runtimeDiagnosticsSnapshotBefore = runtimeWorkspace.value().project().snapshot();
  GRAPPLE_REQUIRE(runtimeDiagnosticsSnapshotBefore);
  const std::string serializedRuntimeDiagnosticsSnapshotBefore =
    project::serializeCanonicalProjectSnapshot(runtimeDiagnosticsSnapshotBefore.value());
  const auto runtimeDiagnosticsQuery = runtimeWorkspace.value().query(project::InspectRuntimeDiagnosticsQuery{});
  GRAPPLE_REQUIRE(runtimeDiagnosticsQuery);
  const auto* runtimeDiagnostics = std::get_if<project::RuntimeInspectDiagnosticsResult>(&runtimeDiagnosticsQuery.value());
  GRAPPLE_REQUIRE(runtimeDiagnostics != nullptr);
  GRAPPLE_REQUIRE(runtimeDiagnostics->revision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(runtimeDiagnostics->diagnostics.empty());
  const auto runtimeDiagnosticsSnapshotAfter = runtimeWorkspace.value().project().snapshot();
  GRAPPLE_REQUIRE(runtimeDiagnosticsSnapshotAfter);
  GRAPPLE_REQUIRE(
    project::serializeCanonicalProjectSnapshot(runtimeDiagnosticsSnapshotAfter.value()) ==
    serializedRuntimeDiagnosticsSnapshotBefore
  );
  CountingCameraTransformRuntime countedRuntime;
  runtime::RuntimeEvaluator countedEvaluator{{&countedRuntime}};
  render::LocalRenderCore countedCore{countedEvaluator};
  app::NativePreviewSession countedPreview{
    runtimeWorkspace.value().project(),
    countedCore
  };
  const auto countedRefresh = countedPreview.refreshFromProject();
  GRAPPLE_REQUIRE(countedRefresh);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  const auto repeatedCountedRefresh = countedPreview.refreshFromProject();
  GRAPPLE_REQUIRE(repeatedCountedRefresh);
  GRAPPLE_REQUIRE(repeatedCountedRefresh.value().preparedPlanHash == countedRefresh.value().preparedPlanHash);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  app::NativeExportSession countedExport{
    runtimeWorkspace.value().project(),
    countedCore
  };
  const auto countedPrepare = countedExport.prepareFromProject();
  GRAPPLE_REQUIRE(countedPrepare);
  GRAPPLE_REQUIRE(countedPrepare.value().preparedPlanHash == countedRefresh.value().preparedPlanHash);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  const auto repeatedCountedPrepare = countedExport.prepareFromProject();
  GRAPPLE_REQUIRE(repeatedCountedPrepare);
  GRAPPLE_REQUIRE(repeatedCountedPrepare.value().preparedPlanHash == countedPrepare.value().preparedPlanHash);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  const auto runtimeRefresh = runtimeWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(runtimeRefresh);
  const auto runtimeFrame = runtimeWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(runtimeFrame);
  GRAPPLE_REQUIRE(runtimeFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras.size() == 1);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].cameraNodeId == runtimeCameraNodeId);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].transform.position.x == 0.25);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].transform.position.y == 0.0);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].transform.scale.x == 1.5);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].transform.scale.y == 1.5);
  const auto runtimeExportPrepare = runtimeWorkspace.value().exportSession().prepareFromProject();
  GRAPPLE_REQUIRE(runtimeExportPrepare);
  const auto runtimeExport = runtimeWorkspace.value().exportSession().render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-runtime-export.mov"}
  });
  GRAPPLE_REQUIRE(runtimeExport);
  GRAPPLE_REQUIRE(runtimeExport.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(runtimeExport.value().framesEvaluated == 2);

  runtime::RuntimeEvaluator appRuntime;
  render::LocalRenderCore appRenderCore{appRuntime};
  app::NativePreviewSession preview{session, appRenderCore};
  const auto frameBeforeRefresh = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(!frameBeforeRefresh);
  GRAPPLE_REQUIRE(frameBeforeRefresh.error().code == "render.plan_missing");

  const auto refresh = preview.refreshFromProject();
  GRAPPLE_REQUIRE(refresh);
  GRAPPLE_REQUIRE(refresh.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(preview.state().core.hasPlan);
  GRAPPLE_REQUIRE(preview.state().core.preparedPlanHash == refresh.value().preparedPlanHash);

  const auto seek = preview.seek(foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(seek);
  const auto frame = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(frame);
  GRAPPLE_REQUIRE(frame.value().frame.description == "layers=0 clips=0 audioClips=0 cameras=0 effects=0");
  GRAPPLE_REQUIRE(frame.value().frame.mediaFrames.empty());
  GRAPPLE_REQUIRE(frame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(frame.value().renderDiagnostics.empty());

  app::NativeExportSession exportSession{session, appRenderCore};
  const auto exportAfterPreviewRefresh = exportSession.render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export.mov"}
  });
  GRAPPLE_REQUIRE(exportAfterPreviewRefresh);
  GRAPPLE_REQUIRE(exportAfterPreviewRefresh.value().framesEvaluated == 2);

  const auto exportPrepare = exportSession.prepareFromProject();
  GRAPPLE_REQUIRE(exportPrepare);
  GRAPPLE_REQUIRE(exportPrepare.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(exportSession.state().core.hasPlan);
  GRAPPLE_REQUIRE(exportSession.state().core.preparedPlanHash == exportPrepare.value().preparedPlanHash);
  const auto projectBeforeExport = session.snapshot();
  GRAPPLE_REQUIRE(projectBeforeExport);
  GRAPPLE_REQUIRE(projectBeforeExport.value().revision == foundation::RevisionId{"rev_1"});
  const std::string serializedProjectBeforeExport =
    project::serializeCanonicalProjectSnapshot(projectBeforeExport.value());

  const auto exportResult = exportSession.render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export.mov"}
  });
  GRAPPLE_REQUIRE(exportResult);
  GRAPPLE_REQUIRE(exportResult.value().outputPath.value == "/tmp/app-export.mov");
  GRAPPLE_REQUIRE(exportResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(exportResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(exportResult.value().renderDiagnostics.empty());
  GRAPPLE_REQUIRE(exportSession.state().lastOutputPath->value == "/tmp/app-export.mov");
  GRAPPLE_REQUIRE(exportSession.state().core.preparedPlanHash == exportPrepare.value().preparedPlanHash);

  const auto changedExportResolution = exportSession.render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1280, 720},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export-720.mov"}
  });
  GRAPPLE_REQUIRE(changedExportResolution);
  GRAPPLE_REQUIRE(exportSession.state().core.preparedPlanHash == exportPrepare.value().preparedPlanHash);
  GRAPPLE_REQUIRE((exportSession.state().lastSettings->resolution == foundation::Resolution{1280, 720}));
  const auto projectAfterExport = session.snapshot();
  GRAPPLE_REQUIRE(projectAfterExport);
  GRAPPLE_REQUIRE(projectAfterExport.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(projectAfterExport.value()) == serializedProjectBeforeExport);

  const auto savedInitial = savedSession.snapshot();
  GRAPPLE_REQUIRE(savedInitial);
  auto savedWorkspace = app::NativeWorkspaceSession::fromProject(std::move(savedSession));
  GRAPPLE_REQUIRE(savedWorkspace);
  app::NativeProjectCommandWriter& savedWriter = savedWorkspace.value().commandWriter();
  const auto savedComposition = savedWriter.apply(
    project::CreateCompositionCommand{savedWriter.nextNodeId("saved composition"), "Saved Main"},
    userSource(),
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_saved_rev_1"},
      foundation::FilePath{"snapshots/rev_1.json"},
      std::optional<std::string>{"saved"}
    }
  );
  GRAPPLE_REQUIRE(savedComposition);

  const auto savedWrite = savedWorkspace.value().writePackage();
  GRAPPLE_REQUIRE(savedWrite);
  GRAPPLE_REQUIRE(savedWrite.value().project.snapshotPath.value == (packageRoot / "snapshots/rev_1.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().project.manifestPath.value == (packageRoot / "manifest.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().project.commandLogPath.value == (packageRoot / "history/commands.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().project.eventLogPath.value == (packageRoot / "history/events.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().project.schemaMigrationLogPath.value == (packageRoot / "history/schema_migrations.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().agentRunsPath.value == (packageRoot / "agent/runs.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().agentEventsPath.value == (packageRoot / "agent/events.json").lexically_normal().string());

  std::ifstream savedSnapshotFile{savedWrite.value().project.snapshotPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedSnapshotFile.good());
  std::ostringstream savedSnapshotContents;
  savedSnapshotContents << savedSnapshotFile.rdbuf();
  GRAPPLE_REQUIRE(savedSnapshotContents.str() == project::serializeCanonicalProjectSnapshot(savedComposition.value().snapshot));
  const auto parsedSavedSnapshot = project::deserializeCanonicalProjectSnapshot(savedSnapshotContents.str());
  GRAPPLE_REQUIRE(parsedSavedSnapshot);
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(parsedSavedSnapshot.value()) == savedSnapshotContents.str());
  std::ifstream savedCommandLogFile{savedWrite.value().project.commandLogPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedCommandLogFile.good());
  std::ostringstream savedCommandLogContents;
  savedCommandLogContents << savedCommandLogFile.rdbuf();
  GRAPPLE_REQUIRE(savedCommandLogContents.str() == history::serializeCanonicalCommandLog(savedWorkspace.value().project().packageState().commandLog));
  std::ifstream savedEventLogFile{savedWrite.value().project.eventLogPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedEventLogFile.good());
  std::ostringstream savedEventLogContents;
  savedEventLogContents << savedEventLogFile.rdbuf();
  GRAPPLE_REQUIRE(savedEventLogContents.str() == history::serializeCanonicalEventLog(savedWorkspace.value().project().packageState().eventLog));
  std::ifstream savedSchemaMigrationLogFile{savedWrite.value().project.schemaMigrationLogPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedSchemaMigrationLogFile.good());
  std::ostringstream savedSchemaMigrationLogContents;
  savedSchemaMigrationLogContents << savedSchemaMigrationLogFile.rdbuf();
  GRAPPLE_REQUIRE(savedSchemaMigrationLogContents.str() == "[]");
  std::ifstream savedAgentRunsFile{savedWrite.value().agentRunsPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedAgentRunsFile.good());
  std::ostringstream savedAgentRunsContents;
  savedAgentRunsContents << savedAgentRunsFile.rdbuf();
  GRAPPLE_REQUIRE(savedAgentRunsContents.str() == "[]");
  std::ifstream savedAgentEventsFile{savedWrite.value().agentEventsPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedAgentEventsFile.good());
  std::ostringstream savedAgentEventsContents;
  savedAgentEventsContents << savedAgentEventsFile.rdbuf();
  GRAPPLE_REQUIRE(savedAgentEventsContents.str() == "[]");

  const auto savedManifest = storage::buildProjectPackageManifest(savedWorkspace.value().project().packageState());
  GRAPPLE_REQUIRE(savedManifest);
  std::ifstream savedManifestFile{savedWrite.value().project.manifestPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedManifestFile.good());
  std::ostringstream savedManifestContents;
  savedManifestContents << savedManifestFile.rdbuf();
  GRAPPLE_REQUIRE(savedManifestContents.str() == storage::serializeCanonicalProjectPackageManifest(savedManifest.value()));
  const storage::ProjectPackageReader reader;
  const auto readLogs = reader.readHistoryLogs(savedWorkspace.value().project().packageState().package);
  GRAPPLE_REQUIRE(readLogs);
  GRAPPLE_REQUIRE(history::serializeCanonicalCommandLog(readLogs.value().commandLog) == savedCommandLogContents.str());
  GRAPPLE_REQUIRE(history::serializeCanonicalEventLog(readLogs.value().eventLog) == savedEventLogContents.str());
  auto openedSavedSession = app::NativeProjectSession::openPackage(savedWorkspace.value().project().packageState().package);
  GRAPPLE_REQUIRE(openedSavedSession);
  const auto openedSavedViewModel = openedSavedSession.value().buildViewModel();
  GRAPPLE_REQUIRE(openedSavedViewModel);
  GRAPPLE_REQUIRE(openedSavedViewModel.value().project.projectId == foundation::ProjectId{"proj_app_saved"});
  GRAPPLE_REQUIRE(openedSavedViewModel.value().project.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(openedSavedViewModel.value().timeline.compositions.size() == 1);
  GRAPPLE_REQUIRE(openedSavedSession.value().packageState().commandLog.records().size() == 1);
  app::NativeProjectCommandWriter openedWriter{openedSavedSession.value()};
  const auto openedTrack = openedWriter.apply(
    project::CreateTrackCommand{
      openedWriter.nextNodeId("opened track"),
      openedSavedViewModel.value().timeline.compositions[0].sourceNodeId,
      openedWriter.nextEdgeId("contains opened track"),
      "Opened Track",
      timeline::TrackKind::Visual
    },
    userSource()
  );
  GRAPPLE_REQUIRE(openedTrack);
  GRAPPLE_REQUIRE(openedTrack.value().snapshot.revision == foundation::RevisionId{"rev_2"});

  auto openedWorkspace = app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{packageRoot.string()});
  GRAPPLE_REQUIRE(openedWorkspace);
  const auto workspaceViewModel = openedWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(workspaceViewModel);
  GRAPPLE_REQUIRE(workspaceViewModel.value().project.projectId == foundation::ProjectId{"proj_app_saved"});
  GRAPPLE_REQUIRE(workspaceViewModel.value().project.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(openedWorkspace.value().project().packageState().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(openedWorkspace.value().mediaSources().sources().empty());
  const auto workspaceRefresh = openedWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(workspaceRefresh);
  GRAPPLE_REQUIRE(openedWorkspace.value().preview().state().core.hasPlan);
  const auto workspaceTrack = openedWorkspace.value().commandWriter().apply(
    project::CreateTrackCommand{
      openedWorkspace.value().commandWriter().nextNodeId("workspace track"),
      workspaceViewModel.value().timeline.compositions[0].sourceNodeId,
      openedWorkspace.value().commandWriter().nextEdgeId("contains workspace track"),
      "Workspace Track",
      timeline::TrackKind::Visual
    },
    userSource()
  );
  GRAPPLE_REQUIRE(workspaceTrack);
  GRAPPLE_REQUIRE(workspaceTrack.value().snapshot.revision == foundation::RevisionId{"rev_2"});
  const auto workspaceWriteWithHistory = openedWorkspace.value().writePackage();
  GRAPPLE_REQUIRE(workspaceWriteWithHistory);
  auto reopenedWorkspaceWithHistory = app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{packageRoot.string()});
  GRAPPLE_REQUIRE(reopenedWorkspaceWithHistory);
  GRAPPLE_REQUIRE(reopenedWorkspaceWithHistory.value().project().packageState().snapshotDocuments.size() == 2);
  GRAPPLE_REQUIRE(reopenedWorkspaceWithHistory.value().project().packageState().snapshots.records().size() == 2);
  const auto reopenedWorkspaceUndo = reopenedWorkspaceWithHistory.value().commandWriter().undoLastCommittedCommand(
    userSource(),
    std::optional<std::string>{"undo reopened track"}
  );
  GRAPPLE_REQUIRE(reopenedWorkspaceUndo);
  GRAPPLE_REQUIRE(reopenedWorkspaceUndo.value().snapshot.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedWorkspaceUndo.value().snapshot.graph.nodes().size() == 1);
  GRAPPLE_REQUIRE(reopenedWorkspaceUndo.value().snapshot.graph.edges().empty());
  std::filesystem::remove_all(packageRoot);

  app::NativeProjectSession projectOnlySession{
    foundation::ProjectId{"proj_app_project_only"},
    "Project Only App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_project_only"},
      foundation::FilePath{projectOnlyPackageRoot.string()},
      1
    }
  };
  app::NativeProjectCommandWriter projectOnlyWriter{projectOnlySession};
  const auto projectOnlyComposition = projectOnlyWriter.apply(
    project::CreateCompositionCommand{projectOnlyWriter.nextNodeId("project only composition"), "Project Only Main"},
    userSource(),
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_project_only_rev_1"},
      foundation::FilePath{"snapshots/rev_1.json"},
      std::optional<std::string>{"project only"}
    }
  );
  GRAPPLE_REQUIRE(projectOnlyComposition);
  const auto projectOnlyWrite = projectOnlySession.writePackage();
  GRAPPLE_REQUIRE(projectOnlyWrite);
  GRAPPLE_REQUIRE(!std::filesystem::exists(projectOnlyPackageRoot / "agent/runs.json"));
  GRAPPLE_REQUIRE(!std::filesystem::exists(projectOnlyPackageRoot / "agent/events.json"));

  auto openedProjectOnlyWorkspace = app::NativeWorkspaceSession::openPackageRoot(
    foundation::FilePath{projectOnlyPackageRoot.string()}
  );
  GRAPPLE_REQUIRE(openedProjectOnlyWorkspace);
  const agent::AgentConversationState projectOnlyConversation =
    openedProjectOnlyWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(projectOnlyConversation.runs.empty());
  GRAPPLE_REQUIRE(projectOnlyConversation.diagnostics.empty());

  std::filesystem::create_directories(projectOnlyPackageRoot / "agent");
  std::ofstream orphanRunsFile{projectOnlyPackageRoot / "agent/runs.json", std::ios::binary | std::ios::trunc};
  orphanRunsFile << "[]";
  orphanRunsFile.close();
  auto openedIncompleteSidecar = app::NativeWorkspaceSession::openPackageRoot(
    foundation::FilePath{projectOnlyPackageRoot.string()}
  );
  GRAPPLE_REQUIRE(!openedIncompleteSidecar);
  GRAPPLE_REQUIRE(openedIncompleteSidecar.error().code == "app.package_agent_sidecar_incomplete");
  std::filesystem::remove_all(projectOnlyPackageRoot);

  app::NativeProjectSession stewardProject{
    foundation::ProjectId{"proj_app_steward"},
    "Steward App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward"},
      foundation::FilePath{stewardPackageRoot.string()},
      1
    }
  };
  auto stewardWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardProject));
  GRAPPLE_REQUIRE(stewardWorkspace);
  const foundation::NodeId stewardCompositionNodeId = stewardWorkspace.value().commandWriter().nextNodeId("composition");
  const auto stewardComposition = stewardWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{stewardCompositionNodeId, "Steward Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(stewardComposition);
  const foundation::NodeId stewardCameraNodeId = stewardWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardCamera = stewardWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardCameraNodeId,
      stewardCompositionNodeId,
      stewardWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{"Camera", timeline::Transform{}, timeline::CameraLens{35.0}}
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardCamera);
  const std::string durableIntent = "Keep the subject centered with editable controls.";
  const auto stewardEffect = stewardWorkspace.value().steward().createCameraTransformEffect(
    stewardCameraNodeId,
    durableIntent,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}}
  );
  GRAPPLE_REQUIRE(stewardEffect);
  const agent::AgentConversationState stewardConversationBeforeSave = stewardWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardConversationBeforeSave.runs.size() == 1);
  GRAPPLE_REQUIRE(stewardConversationBeforeSave.runs[0].status == agent::AgentRunStatus::Succeeded);
  const auto stewardWrite = stewardWorkspace.value().writePackage();
  GRAPPLE_REQUIRE(stewardWrite);
  GRAPPLE_REQUIRE(stewardWrite.value().agentRunsPath.value == (stewardPackageRoot / "agent/runs.json").lexically_normal().string());
  GRAPPLE_REQUIRE(stewardWrite.value().agentEventsPath.value == (stewardPackageRoot / "agent/events.json").lexically_normal().string());
  auto reopenedStewardWorkspace = app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{stewardPackageRoot.string()});
  GRAPPLE_REQUIRE(reopenedStewardWorkspace);
  const agent::AgentConversationState reopenedStewardConversation = reopenedStewardWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(reopenedStewardConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].toolCalls[0].toolSerializedId == "steward.create_camera_transform");
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_1"});
  const auto reopenedStewardViewModel = reopenedStewardWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(reopenedStewardViewModel);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().project.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects[0].displayName == "Camera Transform");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].intent == durableIntent);
  const foundation::NodeId reopenedSecondCameraNodeId = reopenedStewardWorkspace.value().commandWriter().nextNodeId("camera");
  const auto reopenedSecondCamera = reopenedStewardWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      reopenedSecondCameraNodeId,
      stewardCompositionNodeId,
      reopenedStewardWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{"Second Camera", timeline::Transform{}, timeline::CameraLens{35.0}}
    },
    userSource()
  );
  GRAPPLE_REQUIRE(reopenedSecondCamera);
  const auto reopenedSecondStewardEffect = reopenedStewardWorkspace.value().steward().createCameraTransformEffect(
    reopenedSecondCameraNodeId,
    "Add editable controls to the second camera.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{2.0}}
  );
  GRAPPLE_REQUIRE(reopenedSecondStewardEffect);
  const agent::AgentConversationState reopenedStewardConversationAfterSecondRun =
    reopenedStewardWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.diagnostics.empty());
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs.size() == 2);
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].runId == foundation::RunId{"run_steward_2"});
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_2"});
  std::filesystem::remove_all(stewardPackageRoot);

  app::NativeProjectSession noteSession{
    foundation::ProjectId{"proj_app_notes"},
    "Notes App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_notes"},
      foundation::FilePath{"notes-app.grapple"},
      1
    }
  };
  app::NativeProjectCommandWriter noteWriter{noteSession};
  const auto note = noteWriter.apply(
    project::CreateNoteCommand{
      noteWriter.nextNodeId("note"),
      timeline::NotePayload{"Camera rationale", "Keep the camera offset exposed as a parameter."}
    },
    userSource()
  );
  GRAPPLE_REQUIRE(note);
  const auto noteViewModel = noteSession.buildViewModel();
  GRAPPLE_REQUIRE(noteViewModel);
  GRAPPLE_REQUIRE(noteViewModel.value().notes.rows.size() == 1);
  GRAPPLE_REQUIRE(noteViewModel.value().notes.rows[0].sourceNodeId == foundation::NodeId{"node_note_1"});
  GRAPPLE_REQUIRE(noteViewModel.value().notes.rows[0].title == "Camera rationale");
  GRAPPLE_REQUIRE(noteViewModel.value().notes.rows[0].markdown == "Keep the camera offset exposed as a parameter.");
  const auto notesQuery = noteSession.query(project::ListNotesQuery{});
  GRAPPLE_REQUIRE(notesQuery);
  const auto* notesResult = std::get_if<project::NotesResult>(&notesQuery.value());
  GRAPPLE_REQUIRE(notesResult != nullptr);
  GRAPPLE_REQUIRE(notesResult->revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(notesResult->notes.size() == 1);
  GRAPPLE_REQUIRE(notesResult->notes[0].nodeId == foundation::NodeId{"node_note_1"});
  GRAPPLE_REQUIRE(notesResult->notes[0].title == "Camera rationale");
  GRAPPLE_REQUIRE(notesResult->notes[0].markdown == "Keep the camera offset exposed as a parameter.");

  const auto firstCommandId = session.packageState().commandLog.records()[0].id;
  const auto duplicate = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      firstCommandId,
      foundation::ProjectId{"proj_app"},
      composition.value().snapshot.revision,
      userSource(),
      project::CreateCompositionCommand{foundation::NodeId{"node_other"}, "Other"}
    },
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      storage::SnapshotCommitRecord{
        foundation::SnapshotId{"snap_duplicate_rev_2"},
        foundation::FilePath{"snapshots/rev_1.json"},
        std::optional<std::string>{"duplicate"}
      }
    }
  );
  GRAPPLE_REQUIRE(!duplicate);
  GRAPPLE_REQUIRE(duplicate.error().code == "history.command_id_duplicate");
  const auto afterDuplicate = session.snapshot();
  GRAPPLE_REQUIRE(afterDuplicate);
  GRAPPLE_REQUIRE(afterDuplicate.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == 1);
  std::filesystem::remove_all(appPackageRoot);

  return 0;
}
