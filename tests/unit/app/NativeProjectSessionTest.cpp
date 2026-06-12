#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/history/HistorySerializer.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>

#include <TestAssert.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <variant>

namespace {

grapple::project::CommandSource userSource() {
  return grapple::project::CommandSource{
    grapple::project::CommandSourceKind::User,
    std::nullopt,
    "test"
  };
}

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
  GRAPPLE_REQUIRE(session.packageState().head.has_value());
  GRAPPLE_REQUIRE(session.packageState().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().head->lastSnapshotId == foundation::SnapshotId{"snap_cmd_app_1_1"});
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(session.packageState().snapshots.records().size() == 1);

  const auto snapshotQuery = session.query(project::GetProjectSnapshotQuery{});
  GRAPPLE_REQUIRE(snapshotQuery);
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&snapshotQuery.value());
  GRAPPLE_REQUIRE(snapshotResult != nullptr);
  GRAPPLE_REQUIRE(snapshotResult->snapshot.revision == foundation::RevisionId{"rev_1"});

  const auto graphQuery = session.query(project::GetGraphQuery{});
  GRAPPLE_REQUIRE(graphQuery);
  const auto* graphResult = std::get_if<project::GraphResult>(&graphQuery.value());
  GRAPPLE_REQUIRE(graphResult != nullptr);
  GRAPPLE_REQUIRE(graphResult->graph.nodes().size() == 1);

  const auto timeline = session.buildTimelineIR();
  GRAPPLE_REQUIRE(timeline);
  GRAPPLE_REQUIRE(timeline.value().timeline.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(timeline.value().timeline.layers.empty());

  const auto renderPlan = session.buildRenderPlan();
  GRAPPLE_REQUIRE(renderPlan);
  GRAPPLE_REQUIRE(renderPlan.value().plan.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(renderPlan.value().plan.effectGraphs.empty());

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

  const auto manifest = storage::buildProjectPackageManifest(session.packageState());
  GRAPPLE_REQUIRE(manifest);
  GRAPPLE_REQUIRE(manifest.value().head.has_value());
  GRAPPLE_REQUIRE(manifest.value().head->lastCommandId == foundation::CommandId{"cmd_app_1"});
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot.has_value());
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot->id == foundation::SnapshotId{"snap_cmd_app_1_1"});
  GRAPPLE_REQUIRE(manifest.value().latestSnapshot->revision == foundation::RevisionId{"rev_1"});

  const auto writeCurrentSnapshot = session.writePackage();
  GRAPPLE_REQUIRE(writeCurrentSnapshot);
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().snapshotPath.value == (appPackageRoot / "snapshots/snap_cmd_app_1_1.json").lexically_normal().string());
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().manifestPath.value == (appPackageRoot / "manifest.json").lexically_normal().string());
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().commandLogPath.value == (appPackageRoot / "history/commands.json").lexically_normal().string());
  GRAPPLE_REQUIRE(writeCurrentSnapshot.value().eventLogPath.value == (appPackageRoot / "history/events.json").lexically_normal().string());

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
  const auto effectCamera = effectWriter.apply(
    project::CreateCameraCommand{
      effectCameraNodeId,
      effectCompositionNodeId,
      effectWriter.nextEdgeId("contains camera"),
      timeline::CameraPayload{"Camera", timeline::Transform{}, timeline::CameraLens{35.0}}
    },
    userSource()
  );
  GRAPPLE_REQUIRE(effectCamera);
  const auto effectCommand = effectWriter.apply(
    project::CreateEffectCommand{
      effectWriter.nextNodeId("effect"),
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
          {timeline::Param{"target_x", 0.5}}
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
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].targetNodeId == effectCameraNodeId);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].nodeCount == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].edgeCount == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].targetNodeId == effectCameraNodeId);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].displayName == "Camera Follow");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].implementationKind == "python");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].entrypoint == "prepare");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params.size() == 1);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].name == "target_x");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value == "0.5");

  app::NativePreviewSession preview{session};
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
  GRAPPLE_REQUIRE(frame.value().frame.description == "layers=0 clips=0 cameras=0 effects=0");
  GRAPPLE_REQUIRE(frame.value().frame.mediaFrames.empty());
  GRAPPLE_REQUIRE(frame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(frame.value().renderDiagnostics.empty());

  app::NativeExportSession exportSession{session};
  const auto exportBeforePrepare = exportSession.render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"test"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export.mov"}
  });
  GRAPPLE_REQUIRE(!exportBeforePrepare);
  GRAPPLE_REQUIRE(exportBeforePrepare.error().code == "render.plan_missing");

  const auto exportPrepare = exportSession.prepareFromProject();
  GRAPPLE_REQUIRE(exportPrepare);
  GRAPPLE_REQUIRE(exportPrepare.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(exportSession.state().core.hasPlan);
  GRAPPLE_REQUIRE(exportSession.state().core.preparedPlanHash == exportPrepare.value().preparedPlanHash);

  const auto exportResult = exportSession.render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"test"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export.mov"}
  });
  GRAPPLE_REQUIRE(exportResult);
  GRAPPLE_REQUIRE(exportResult.value().outputPath.value == "/tmp/app-export.mov");
  GRAPPLE_REQUIRE(exportResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(exportResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(exportResult.value().renderDiagnostics.empty());
  GRAPPLE_REQUIRE(exportSession.state().lastOutputPath->value == "/tmp/app-export.mov");

  const auto savedInitial = savedSession.snapshot();
  GRAPPLE_REQUIRE(savedInitial);
  app::NativeProjectCommandWriter savedWriter{savedSession};
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

  const auto savedWrite = savedSession.writePackage();
  GRAPPLE_REQUIRE(savedWrite);
  GRAPPLE_REQUIRE(savedWrite.value().snapshotPath.value == (packageRoot / "snapshots/rev_1.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().manifestPath.value == (packageRoot / "manifest.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().commandLogPath.value == (packageRoot / "history/commands.json").lexically_normal().string());
  GRAPPLE_REQUIRE(savedWrite.value().eventLogPath.value == (packageRoot / "history/events.json").lexically_normal().string());

  std::ifstream savedSnapshotFile{savedWrite.value().snapshotPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedSnapshotFile.good());
  std::ostringstream savedSnapshotContents;
  savedSnapshotContents << savedSnapshotFile.rdbuf();
  GRAPPLE_REQUIRE(savedSnapshotContents.str() == project::serializeCanonicalProjectSnapshot(savedComposition.value().snapshot));
  const auto parsedSavedSnapshot = project::deserializeCanonicalProjectSnapshot(savedSnapshotContents.str());
  GRAPPLE_REQUIRE(parsedSavedSnapshot);
  GRAPPLE_REQUIRE(project::serializeCanonicalProjectSnapshot(parsedSavedSnapshot.value()) == savedSnapshotContents.str());
  std::ifstream savedCommandLogFile{savedWrite.value().commandLogPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedCommandLogFile.good());
  std::ostringstream savedCommandLogContents;
  savedCommandLogContents << savedCommandLogFile.rdbuf();
  GRAPPLE_REQUIRE(savedCommandLogContents.str() == history::serializeCanonicalCommandLog(savedSession.packageState().commandLog));
  std::ifstream savedEventLogFile{savedWrite.value().eventLogPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedEventLogFile.good());
  std::ostringstream savedEventLogContents;
  savedEventLogContents << savedEventLogFile.rdbuf();
  GRAPPLE_REQUIRE(savedEventLogContents.str() == history::serializeCanonicalEventLog(savedSession.packageState().eventLog));

  const auto savedManifest = storage::buildProjectPackageManifest(savedSession.packageState());
  GRAPPLE_REQUIRE(savedManifest);
  std::ifstream savedManifestFile{savedWrite.value().manifestPath.value, std::ios::binary};
  GRAPPLE_REQUIRE(savedManifestFile.good());
  std::ostringstream savedManifestContents;
  savedManifestContents << savedManifestFile.rdbuf();
  GRAPPLE_REQUIRE(savedManifestContents.str() == storage::serializeCanonicalProjectPackageManifest(savedManifest.value()));
  const storage::ProjectPackageReader reader;
  const auto readLogs = reader.readHistoryLogs(savedSession.packageState().package);
  GRAPPLE_REQUIRE(readLogs);
  GRAPPLE_REQUIRE(history::serializeCanonicalCommandLog(readLogs.value().commandLog) == savedCommandLogContents.str());
  GRAPPLE_REQUIRE(history::serializeCanonicalEventLog(readLogs.value().eventLog) == savedEventLogContents.str());
  auto openedSavedSession = app::NativeProjectSession::openPackage(savedSession.packageState().package);
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
      "Opened Track"
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
      "Workspace Track"
    },
    userSource()
  );
  GRAPPLE_REQUIRE(workspaceTrack);
  GRAPPLE_REQUIRE(workspaceTrack.value().snapshot.revision == foundation::RevisionId{"rev_2"});
  std::filesystem::remove_all(packageRoot);

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
