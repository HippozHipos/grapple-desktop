#include <DemoProject.hpp>

#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/projection/RenderPlanSerializer.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>

#include <iostream>
#include <optional>
#include <string>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
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

  bool printRenderPlanJson = false;
  bool printPreviewFrame = false;
  bool runExportSmoke = false;
  bool savePackage = false;
  bool openPackageSmoke = false;
  if (argc == 2) {
    const std::string argument{argv[1]};
    if (argument == "--render-plan-json") {
      printRenderPlanJson = true;
    } else if (argument == "--preview-frame") {
      printPreviewFrame = true;
    } else if (argument == "--export-smoke") {
      runExportSmoke = true;
    } else if (argument == "--save-package") {
      savePackage = true;
    } else if (argument == "--open-package-smoke") {
      savePackage = true;
      openPackageSmoke = true;
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return 1;
    }
  } else if (argc > 2) {
    std::cerr << "Expected zero arguments, --render-plan-json, --preview-frame, --export-smoke, --save-package, or --open-package-smoke.\n";
    return 1;
  }

  app::NativeProjectSession session{
    foundation::ProjectId{"proj_cli"},
    "CLI Smoke Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_cli"},
      foundation::FilePath{savePackage ? "/tmp/grapple-cli-package" : "cli.grapple"},
      1
    }
  };

  const auto demoProject = demo::populateWalkingWomanDemo(
    session,
    savePackage
      ? std::optional<storage::SnapshotCommitRecord>{storage::SnapshotCommitRecord{
          foundation::SnapshotId{"snap_cli_rev_6"},
          foundation::FilePath{"snapshots/rev_6.json"},
          std::optional<std::string>{"cli"}
        }}
      : std::nullopt
  );
  if (!demoProject) {
    printError(demoProject.error());
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
    app::NativePreviewSession preview{session};
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
    return 0;
  }

  if (runExportSmoke) {
    app::NativeExportSession exportSession{session};
    const auto prepare = exportSession.prepareFromProject();
    if (!prepare) {
      printError(prepare.error());
      return 1;
    }

    const auto result = exportSession.render(render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::FrameRate{2, 1},
      foundation::Resolution{1920, 1080},
      render::Codec{"test"},
      render::RenderQuality::Final,
      foundation::FilePath{"/tmp/grapple-cli-export.mov"}
    });
    if (!result) {
      printError(result.error());
      return 1;
    }

    std::cout << "revision=" << prepare.value().revision.value() << '\n';
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
    if (openPackageSmoke) {
      auto opened = app::NativeProjectSession::openPackage(storage::ProjectPackage{
        foundation::ProjectId{"proj_cli"},
        foundation::FilePath{"/tmp/grapple-cli-package"},
        1
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
