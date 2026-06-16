#include <DemoProject.hpp>

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/project/ProjectMediaPlacement.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/render/LocalRenderCore.hpp>
#include <grapple/render/LocalRenderSystem.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#ifndef GRAPPLE_NATIVE_VERSION
#error "GRAPPLE_NATIVE_VERSION must be defined by CMake"
#endif

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

void printHelp(std::ostream& output) {
  output
    << "Grapple CLI " << GRAPPLE_NATIVE_VERSION << "\n\n"
    << "Usage:\n"
    << "  grapple-cli\n"
    << "  grapple-cli --version\n"
    << "  grapple-cli --help\n"
    << "  grapple-cli --import-media <file> --export <file>\n"
    << "  grapple-cli --render-plan-json\n"
    << "  grapple-cli --preview-frame\n"
    << "  grapple-cli --export-smoke\n"
    << "  grapple-cli --save-package <dir>\n"
    << "  grapple-cli --open-package-smoke <dir>\n";
}

grapple::project::CommandSource cliUserSource() {
  return grapple::project::CommandSource{
    grapple::project::CommandSourceKind::User,
    std::nullopt,
    "cli"
  };
}

grapple::foundation::Result<void> placeImportedMediaOnTimeline(
  grapple::app::NativeWorkspaceSession& workspace,
  const grapple::foundation::AssetId& assetId
) {
  auto snapshot = workspace.project().snapshot();
  if (!snapshot) {
    return snapshot.error();
  }
  const grapple::asset::Asset* asset = snapshot.value().assets.find(assetId);
  if (asset == nullptr) {
    return grapple::foundation::Error{
      "cli.imported_asset_missing",
      "Imported media asset is not present in the project snapshot."
    };
  }
  auto compositions = grapple::project::inspectCompositions(snapshot.value());
  if (!compositions) {
    return compositions.error();
  }
  auto placement = grapple::project::buildMediaPlacementDraft(
    workspace.commandWriter(),
    *asset,
    std::nullopt,
    std::nullopt,
    compositions.value().compositions
  );
  if (!placement) {
    return placement.error();
  }
  auto applied = workspace.commandWriter().apply(
    placement.value().command,
    cliUserSource()
  );
  if (!applied) {
    return applied.error();
  }
  return {};
}

grapple::foundation::Result<grapple::render::ExportSettings> exportSettingsForImportedTimeline(
  const grapple::app::AppViewModel& viewModel,
  grapple::foundation::FilePath outputPath
) {
  if (viewModel.timeline.duration.value <= 0.0) {
    return grapple::foundation::Error{
      "cli.export_timeline_empty",
      "CLI export requires imported visual media on the timeline."
    };
  }
  if (viewModel.assets.rows.empty() || !viewModel.assets.rows.front().dimensions.has_value()) {
    return grapple::foundation::Error{
      "cli.export_visual_asset_missing",
      "CLI export requires imported visual media with dimensions."
    };
  }
  return grapple::render::ExportSettings{
    grapple::foundation::TimeRange{
      grapple::foundation::TimeSeconds{0.0},
      viewModel.timeline.duration
    },
    grapple::foundation::FrameRate{30, 1},
    viewModel.assets.rows.front().dimensions.value(),
    grapple::render::Codec{"mp4v"},
    grapple::render::RenderQuality::Final,
    std::move(outputPath)
  };
}

grapple::foundation::Result<grapple::render::FinalRenderResult> exportImportedMedia(
  const grapple::foundation::FilePath& mediaPath,
  grapple::foundation::FilePath outputPath
) {
  auto workspace = grapple::app::NativeWorkspaceSession::createPackageRoot(
    grapple::foundation::FilePath{(
      std::filesystem::temp_directory_path() /
      ("grapple-cli-import-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
    ).string()},
    "CLI Export"
  );
  if (!workspace) {
    return workspace.error();
  }
  auto imported = workspace.value().importMediaFile(mediaPath);
  if (!imported) {
    return imported.error();
  }
  auto placement = placeImportedMediaOnTimeline(workspace.value(), imported.value());
  if (!placement) {
    return placement.error();
  }
  auto viewModel = workspace.value().project().buildViewModel();
  if (!viewModel) {
    return viewModel.error();
  }
  auto renderPlan = workspace.value().project().buildRenderPlan();
  if (!renderPlan) {
    return renderPlan.error();
  }
  auto settings = exportSettingsForImportedTimeline(viewModel.value(), std::move(outputPath));
  if (!settings) {
    return settings.error();
  }
  return workspace.value().exportSession().renderPlanToVideo(
    renderPlan.value().plan,
    std::move(settings.value())
  );
}

const char* mediaKindText(grapple::render::RenderedMediaKind kind) {
  switch (kind) {
    case grapple::render::RenderedMediaKind::Video:
      return "video";
    case grapple::render::RenderedMediaKind::Image:
      return "image";
  }

  return "unknown";
}

} // namespace

int main(int argc, char* argv[]) {
  using namespace grapple;

  if (argc == 2 && std::string{argv[1]} == "--version") {
    std::cout << "Grapple " << GRAPPLE_NATIVE_VERSION << '\n';
    return 0;
  }
  if (argc == 2 && std::string{argv[1]} == "--help") {
    printHelp(std::cout);
    return 0;
  }

  bool printRenderPlanJson = false;
  bool printPreviewFrame = false;
  bool runExportSmoke = false;
  bool savePackage = false;
  bool openPackageSmoke = false;
  bool exportImported = false;
  std::optional<foundation::FilePath> packagePath;
  std::optional<foundation::FilePath> importMediaPath;
  std::optional<foundation::FilePath> exportPath;
  if (argc == 5 && std::string{argv[1]} == "--import-media" && std::string{argv[3]} == "--export") {
    exportImported = true;
    importMediaPath = foundation::FilePath{argv[2]};
    exportPath = foundation::FilePath{argv[4]};
  } else if (argc == 2 || argc == 3) {
    const std::string argument{argv[1]};
    if (argument == "--render-plan-json") {
      if (argc != 2) {
        std::cerr << "--render-plan-json does not take a package directory.\n";
        return 1;
      }
      printRenderPlanJson = true;
    } else if (argument == "--preview-frame") {
      if (argc != 2) {
        std::cerr << "--preview-frame does not take a package directory.\n";
        return 1;
      }
      printPreviewFrame = true;
    } else if (argument == "--export-smoke") {
      if (argc != 2) {
        std::cerr << "--export-smoke does not take a package directory.\n";
        return 1;
      }
      runExportSmoke = true;
    } else if (argument == "--save-package") {
      savePackage = true;
      if (argc != 3) {
        std::cerr << "--save-package requires a package directory.\n";
        return 1;
      }
      packagePath = foundation::FilePath{argv[2]};
    } else if (argument == "--open-package-smoke") {
      savePackage = true;
      openPackageSmoke = true;
      if (argc != 3) {
        std::cerr << "--open-package-smoke requires a package directory.\n";
        return 1;
      }
      packagePath = foundation::FilePath{argv[2]};
    } else {
      std::cerr << "Unknown argument: " << argument << "\nRun `grapple-cli --help` for usage.\n";
      return 1;
    }
  } else if (argc > 2) {
    std::cerr << "Invalid arguments.\nRun `grapple-cli --help` for usage.\n";
    return 1;
  }

  if (exportImported) {
    auto result = exportImportedMedia(importMediaPath.value(), exportPath.value());
    if (!result) {
      printError(result.error());
      return 1;
    }
    if (!std::filesystem::exists(result.value().outputPath.value) ||
        std::filesystem::file_size(result.value().outputPath.value) == 0) {
      std::cerr << "Export did not write a video artifact.\n";
      return 1;
    }
    std::cout << "output=" << result.value().outputPath.value << '\n';
    std::cout << "revision=" << result.value().sourceRevision.value() << '\n';
    std::cout << "frames=" << result.value().framesEvaluated << '\n';
    return 0;
  }

  app::NativeProjectSession session{
    foundation::ProjectId{"proj_cli"},
    "CLI Smoke Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_cli"},
      packagePath.value_or(foundation::FilePath{"cli.grapple"}),
      storage::CurrentProjectPackageSchemaVersion
    }
  };

  const auto demoProject = demo::populateStarterDemo(
    session,
    savePackage
      ? std::optional<storage::SnapshotCommitRecord>{storage::SnapshotCommitRecord{
          foundation::SnapshotId{"snap_cli_rev_5"},
          foundation::FilePath{"snapshots/rev_5.json"},
          std::optional<std::string>{"cli"}
        }}
      : std::nullopt
  );
  if (!demoProject) {
    printError(demoProject.error());
    return 1;
  }

  const auto demoVideo = demo::ensureStarterDemoVideo();
  if (!demoVideo) {
    printError(demoVideo.error());
    return 1;
  }

  const auto renderPlan = session.buildRenderPlan();
  if (!renderPlan) {
    printError(renderPlan.error());
    return 1;
  }

  if (printRenderPlanJson) {
    std::cout << projection::serializeCanonicalRenderPlan(renderPlan.value().plan) << '\n';
    return 0;
  }

  if (printPreviewFrame) {
    runtime::RuntimeEvaluator runtime;
    render::LocalRenderCore renderCore{runtime};
    render::LocalRenderSystem renderSystem{renderCore};
    app::NativePreviewSession preview{session, renderSystem};
    const auto refresh = preview.refreshFromProject();
    if (!refresh) {
      printError(refresh.error());
      return 1;
    }

    const auto frame = preview.renderFrame(render::RenderFrameRequest{
      foundation::TimeSeconds{0.0},
      render::RenderQuality::Draft
    });
    if (!frame) {
      printError(frame.error());
      return 1;
    }

    std::cout << "revision=" << refresh.value().revision.value() << '\n';
    std::cout << "frameRevision=" << frame.value().frame.sourceRevision.value() << '\n';
    std::cout << "renderPlanHash=" << frame.value().frame.renderPlanHash.toHex() << '\n';
    std::cout << "frame=" << frame.value().frame.description << '\n';
    std::cout << "mediaFrames=" << frame.value().frame.mediaFrames.size() << '\n';
    for (const render::RenderedMediaFrame& mediaFrame : frame.value().frame.mediaFrames) {
      std::cout << "mediaFrame="
                << mediaFrame.clipNodeId.value() << ','
                << mediaFrame.trackNodeId.value() << ','
                << mediaFrame.assetId.value() << ','
                << mediaKindText(mediaFrame.kind) << ','
                << mediaFrame.sourceTime.value
                << '\n';
    }
    std::cout << "audioClips=" << frame.value().frame.audioClips.size() << '\n';
    for (const render::RenderedAudioClip& audioClip : frame.value().frame.audioClips) {
      std::cout << "audioClip="
                << audioClip.clipNodeId.value() << ','
                << audioClip.trackNodeId.value() << ','
                << audioClip.assetId.value() << ','
                << audioClip.timelineRange.start.value << ','
                << audioClip.timelineRange.end.value << ','
                << audioClip.sourceRange.start.value << ','
                << audioClip.sourceRange.end.value << ','
                << audioClip.playbackRate
                << '\n';
    }
    return 0;
  }

  if (runExportSmoke) {
    auto workspace = app::NativeWorkspaceSession::fromProject(std::move(session));
    if (!workspace) {
      printError(workspace.error());
      return 1;
    }
    const auto renderPlan = workspace.value().project().buildRenderPlan();
    if (!renderPlan) {
      printError(renderPlan.error());
      return 1;
    }

    const auto result = workspace.value().exportSession().renderPlanToVideo(
      renderPlan.value().plan,
      render::ExportSettings{
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
        foundation::FrameRate{2, 1},
        foundation::Resolution{1920, 1080},
        render::Codec{"mjpeg"},
        render::RenderQuality::Final,
        foundation::FilePath{"/tmp/grapple-cli-export.avi"}
      }
    );
    if (!result) {
      printError(result.error());
      return 1;
    }
    if (!std::filesystem::exists(result.value().outputPath.value) ||
        std::filesystem::file_size(result.value().outputPath.value) == 0) {
      std::cerr << "Export did not write a video artifact.\n";
      return 1;
    }

    std::cout << "revision=" << renderPlan.value().plan.revision.value() << '\n';
    std::cout << "output=" << result.value().outputPath.value << '\n';
    std::cout << "frames=" << result.value().framesEvaluated << '\n';
    return 0;
  }

  if (savePackage) {
    const auto write = session.writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }

    std::cout << "snapshot=" << write.value().snapshotPath.value << '\n';
    std::cout << "manifest=" << write.value().manifestPath.value << '\n';
    std::cout << "commands=" << write.value().commandLogPath.value << '\n';
    std::cout << "events=" << write.value().eventLogPath.value << '\n';
    std::cout << "schema_migrations=" << write.value().schemaMigrationLogPath.value << '\n';
    if (openPackageSmoke) {
      auto opened = app::NativeProjectSession::openPackage(storage::ProjectPackage{
        foundation::ProjectId{"proj_cli"},
        packagePath.value(),
        storage::CurrentProjectPackageSchemaVersion
      });
      if (!opened) {
        printError(opened.error());
        return 1;
      }
      const auto openedViewModel = opened.value().buildViewModel();
      if (!openedViewModel) {
        printError(openedViewModel.error());
        return 1;
      }
      std::cout << "openedRevision=" << openedViewModel.value().project.revision.value() << '\n';
      std::cout << "openedCommands=" << opened.value().packageState().commandLog.records().size() << '\n';
    }
    return 0;
  }

  const auto viewModel = session.buildViewModel();
  if (!viewModel) {
    printError(viewModel.error());
    return 1;
  }

  std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
  std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
  std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
  std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
  std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
  std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
  std::cout << "effectGraphs=" << viewModel.value().timeline.effectGraphs.size() << '\n';
  std::cout << "diagnostics=" << renderPlan.value().diagnostics.size() << '\n';

  return 0;
}
