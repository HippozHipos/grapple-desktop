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
#include <grapple/runtime/BuiltinEffects.hpp>
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
      "Temporary Track"
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
  const auto restoredTrackRevision = restoreWriter.restoreCommittedRevision(
    foundation::RevisionId{"rev_2"},
    userSource(),
    std::optional<std::string>{"restore track creation"}
  );
  GRAPPLE_REQUIRE(restoredTrackRevision);
  GRAPPLE_REQUIRE(restoredTrackRevision.value().commandResult.beforeRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(restoredTrackRevision.value().commandResult.afterRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(restoredTrackRevision.value().snapshot.graph.nodes().size() == 2);
  GRAPPLE_REQUIRE(restoredTrackRevision.value().snapshot.graph.edges().size() == 1);
  GRAPPLE_REQUIRE(restoreSession.packageState().commandLog.records().size() == 4);
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
      "Asset Track"
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
  GRAPPLE_REQUIRE(assetClipViewModel.value().timeline.clips[0].transform == clipTransform);

  const std::filesystem::path cacheImagePath = writeTinyPpm("grapple_native_cache_image");
  app::NativeProjectSession cacheProject{
    foundation::ProjectId{"proj_app_cache"},
    "Cache App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_cache"},
      foundation::FilePath{"cache-app.grapple"},
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
      "Cache Track"
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
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 0);
  const auto exportOnlyPrepare = cacheWorkspace.value().exportSession().prepareFromProject();
  GRAPPLE_REQUIRE(exportOnlyPrepare);
  const auto exportOnlyResult = cacheWorkspace.value().exportSession().render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{1, 1},
    foundation::Resolution{2, 1},
    render::Codec{"test"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/cache-export.mov"}
  });
  GRAPPLE_REQUIRE(exportOnlyResult);
  GRAPPLE_REQUIRE(exportOnlyResult.value().framesEvaluated == 1);
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 1);
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
  std::filesystem::remove(cacheImagePath);

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
          {timeline::Param{
            "target_x",
            0.5,
            timeline::Param::Control{
              "Target X",
              timeline::Param::NumericControl{0.0, 1.0, 0.01}
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
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].label == "Target X");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value == "0.5");
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].numericMin == 0.0);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].numericMax == 1.0);
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].numericStep == 0.01);

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
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params.size() == 2);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].label == "Position X");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].label == "Position Y");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].intent == "Center the subject with an editable camera transform.");
  const foundation::NodeId runtimeEffectNodeId = runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].sourceNodeId;
  const auto updatedRuntimeEffect = runtimeWorkspace.value().effects().setNumericParam(
    runtimeEffectNodeId,
    runtime::builtin_effect::PositionXParam,
    0.25,
    userSource()
  );
  GRAPPLE_REQUIRE(updatedRuntimeEffect);
  const auto updatedRuntimeEffectViewModel = runtimeWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel);
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value == "0.25");
  const auto runtimeDiagnosticsQuery = runtimeWorkspace.value().query(project::InspectRuntimeDiagnosticsQuery{});
  GRAPPLE_REQUIRE(runtimeDiagnosticsQuery);
  const auto* runtimeDiagnostics = std::get_if<project::RuntimeInspectDiagnosticsResult>(&runtimeDiagnosticsQuery.value());
  GRAPPLE_REQUIRE(runtimeDiagnostics != nullptr);
  GRAPPLE_REQUIRE(runtimeDiagnostics->revision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(runtimeDiagnostics->diagnostics.empty());
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
  const auto runtimeExportPrepare = runtimeWorkspace.value().exportSession().prepareFromProject();
  GRAPPLE_REQUIRE(runtimeExportPrepare);
  const auto runtimeExport = runtimeWorkspace.value().exportSession().render(render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"test"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-runtime-export.mov"}
  });
  GRAPPLE_REQUIRE(runtimeExport);
  GRAPPLE_REQUIRE(runtimeExport.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(runtimeExport.value().framesEvaluated == 2);

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
  const auto stewardWrite = stewardWorkspace.value().project().writePackage();
  GRAPPLE_REQUIRE(stewardWrite);
  auto reopenedStewardWorkspace = app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{stewardPackageRoot.string()});
  GRAPPLE_REQUIRE(reopenedStewardWorkspace);
  const auto reopenedStewardViewModel = reopenedStewardWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(reopenedStewardViewModel);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().project.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects[0].displayName == "Camera Transform");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].intent == durableIntent);
  std::filesystem::remove_all(stewardPackageRoot);

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
