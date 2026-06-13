#include "DesktopApp.hpp"
#include "DesktopWindow.hpp"

#include <DemoProject.hpp>

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>

#include <QApplication>
#include <QPixmap>
#include <QString>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

grapple::foundation::Result<void> ensureDemoVideoFile(const grapple::foundation::FilePath& path) {
  constexpr int width = 320;
  constexpr int height = 180;
  constexpr int frameCount = 300;
  const std::filesystem::path videoPath{path.value};
  std::filesystem::create_directories(videoPath.parent_path());

  cv::VideoWriter writer{
    path.value,
    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
    30.0,
    cv::Size{width, height}
  };
  if (!writer.isOpened()) {
    return grapple::foundation::Error{"desktop.demo_video_open_failed", "Could not create demo video " + path.value + "."};
  }

  for (int frame = 0; frame < frameCount; ++frame) {
    cv::Mat image(height, width, CV_8UC3);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        image.at<cv::Vec3b>(y, x) = cv::Vec3b{
          static_cast<unsigned char>((180 + frame * 2) % 255),
          static_cast<unsigned char>((y * 2 + 80) % 255),
          static_cast<unsigned char>((x + frame * 4) % 255)
        };
      }
    }
    writer.write(image);
  }

  return {};
}


grapple::foundation::Result<void> populateDemo(grapple::app::NativeProjectSession& session, bool savePackage) {
  return grapple::demo::populateWalkingWomanDemo(
    session,
    savePackage
      ? std::optional<grapple::storage::SnapshotCommitRecord>{grapple::storage::SnapshotCommitRecord{
          grapple::foundation::SnapshotId{"snap_desktop_rev_5"},
          grapple::foundation::FilePath{"snapshots/rev_5.json"},
          std::optional<std::string>{"desktop"}
        }}
      : std::nullopt
  );
}


} // namespace

int grapple::desktop::runDesktopApp(int argc, char* argv[]) {
  bool smoke = false;
  bool mutateSmoke = false;
  bool seekSmoke = false;
  bool timelineSeekSmoke = false;
  bool selectSmoke = false;
  bool selectCameraSmoke = false;
  bool stewardSmoke = false;
  bool importSmoke = false;
  bool addVideoSmoke = false;
  bool moveClipSmoke = false;
  bool addEffectSmoke = false;
  bool setEffectParamSmoke = false;
  bool deleteEffectSmoke = false;
  bool deleteSmoke = false;
  bool playbackSmoke = false;
  bool openPackageSmoke = false;
  bool editSaveSmoke = false;
  std::optional<std::string> screenshotPath;
  for (int index = 1; index < argc; ++index) {
    const std::string argument{argv[index]};
    if (argument == "--smoke") {
      smoke = true;
    } else if (argument == "--mutate-smoke") {
      mutateSmoke = true;
    } else if (argument == "--seek-smoke") {
      seekSmoke = true;
    } else if (argument == "--timeline-seek-smoke") {
      timelineSeekSmoke = true;
    } else if (argument == "--select-smoke") {
      selectSmoke = true;
    } else if (argument == "--select-camera-smoke") {
      selectCameraSmoke = true;
    } else if (argument == "--steward-smoke") {
      stewardSmoke = true;
    } else if (argument == "--import-smoke") {
      importSmoke = true;
    } else if (argument == "--add-video-smoke") {
      addVideoSmoke = true;
    } else if (argument == "--move-clip-smoke") {
      moveClipSmoke = true;
    } else if (argument == "--add-effect-smoke") {
      addEffectSmoke = true;
    } else if (argument == "--set-effect-param-smoke") {
      setEffectParamSmoke = true;
    } else if (argument == "--delete-effect-smoke") {
      deleteEffectSmoke = true;
    } else if (argument == "--delete-smoke") {
      deleteSmoke = true;
    } else if (argument == "--playback-smoke") {
      playbackSmoke = true;
    } else if (argument == "--open-package-smoke") {
      openPackageSmoke = true;
    } else if (argument == "--edit-save-smoke") {
      editSaveSmoke = true;
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else {
      std::cerr << "Expected --smoke, --mutate-smoke, --seek-smoke, --timeline-seek-smoke, --select-smoke, --select-camera-smoke, --steward-smoke, --import-smoke, --add-video-smoke, --move-clip-smoke, --add-effect-smoke, --set-effect-param-smoke, --delete-effect-smoke, --delete-smoke, --playback-smoke, --open-package-smoke, --edit-save-smoke, or --screenshot <path>.\n";
      return 1;
    }
  }

  QApplication app{argc, argv};
  grapple::app::NativeProjectSession session{
    grapple::foundation::ProjectId{"proj_desktop"},
    "Desktop Demo",
    grapple::storage::ProjectPackage{
      grapple::foundation::ProjectId{"proj_desktop"},
      grapple::foundation::FilePath{"/tmp/grapple-desktop-package"},
      1
    }
  };
  const auto demoVideo = ensureDemoVideoFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/walking-woman.avi"});
  if (!demoVideo) {
    printError(demoVideo.error());
    return 1;
  }

  const auto populated = populateDemo(session, true);
  if (!populated) {
    printError(populated.error());
    return 1;
  }

  auto workspace = grapple::app::NativeWorkspaceSession::fromProject(std::move(session));
  if (!workspace) {
    printError(workspace.error());
    return 1;
  }

  DesktopWindow window{workspace.value()};

  if (smoke) {
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    return 0;
  }

  if (mutateSmoke) {
    window.addTrack();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    return viewModel.value().timeline.layers.size() == 2 ? 0 : 1;
  }

  if (seekSmoke) {
    window.seekTo(grapple::foundation::TimeSeconds{5.0});
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead == grapple::foundation::TimeSeconds{5.0} ? 0 : 1;
  }

  if (timelineSeekSmoke) {
    window.show();
    app.processEvents();
    window.clickTimelineAtRatio(0.5);
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead.value > 4.9 && previewState.playhead.value < 5.1 ? 0 : 1;
  }

  if (selectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    const auto selectedNodeId = window.selectedNodeId();
    if (!selectedNodeId.has_value()) {
      std::cerr << "No selected timeline node.\n";
      return 1;
    }
    std::cout << "selected=" << selectedNodeId->value() << '\n';
    return selectedNodeId.value() == grapple::foundation::NodeId{"node_clip_3"} ? 0 : 1;
  }

  if (selectCameraSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    const auto selectedNodeId = window.selectedNodeId();
    if (!selectedNodeId.has_value()) {
      std::cerr << "No selected camera node.\n";
      return 1;
    }
    const std::string inspector = window.inspectorContents();
    const std::string log = window.logContents();
    std::cout << "selected=" << selectedNodeId->value() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return selectedNodeId.value() == grapple::foundation::NodeId{"node_camera_4"} &&
           inspector.find("Effects: none") != std::string::npos
      ? 0
      : 1;
  }

  if (stewardSmoke) {
    const std::string steward = window.stewardContents();
    std::cout << "steward=" << steward << '\n';
    return steward.find("Bet: prompt -> editable graph") != std::string::npos &&
           steward.find("Intent: Center the subject with an editable camera transform.") != std::string::npos &&
           steward.find("Action: selected camera -> editable Camera Transform") != std::string::npos &&
           steward.find("Steward edits") != std::string::npos &&
           steward.find("- none yet") != std::string::npos
      ? 0
      : 1;
  }

  if (importSmoke) {
    window.importVideoFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/walking-woman.avi"});
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
    if (window.selectedAssetId().has_value()) {
      std::cout << "selectedAsset=" << window.selectedAssetId()->value() << '\n';
    }
    return viewModel.value().assets.count == 2 &&
           viewModel.value().timeline.clips.size() == 1 &&
           viewModel.value().timeline.duration.value > 9.9 &&
           window.selectedAssetId().has_value()
      ? 0
      : 1;
  }

  if (addVideoSmoke) {
    window.importVideoFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/walking-woman.avi"});
    window.addSelectedVideoToTimeline();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
    return viewModel.value().assets.count == 2 &&
           viewModel.value().timeline.clips.size() == 2 &&
           viewModel.value().timeline.duration.value > 19.9
      ? 0
      : 1;
  }

  if (moveClipSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    window.moveSelectedClip(grapple::foundation::TimeSeconds{1.0});
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.clips.empty()) {
      std::cerr << "No clips after move.\n";
      return 1;
    }
    const grapple::app::AppClipRow& clip = viewModel.value().timeline.clips.front();
    std::cout << "start=" << clip.timelineRange.start.value << '\n';
    std::cout << "end=" << clip.timelineRange.end.value << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
    return clip.timelineRange.start == grapple::foundation::TimeSeconds{1.0} &&
           clip.timelineRange.end == grapple::foundation::TimeSeconds{11.0} &&
           viewModel.value().timeline.duration == grapple::foundation::TimeSeconds{11.0}
      ? 0
      : 1;
  }

  if (setEffectParamSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Shift the camera right with editable controls.");
    window.clickStewardCreateCameraEffect();
    window.setSelectedTargetNumericEffectParam(grapple::runtime::builtin_effect::PositionXParam, 0.25);
    const std::string inspector = window.inspectorContents();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           inspector.find("Position X (position_x)=0.25") != std::string::npos
      ? 0
      : 1;
  }

  if (deleteEffectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Add then remove an editable camera transform.");
    window.clickStewardCreateCameraEffect();
    window.deleteSelectedTargetEffect();
    const std::string inspector = window.inspectorContents();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "effectGraphs=" << viewModel.value().timeline.effectGraphs.size() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           viewModel.value().timeline.effectGraphs.empty() &&
           inspector.find("Effects: none") != std::string::npos
      ? 0
      : 1;
  }

  if (addEffectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Center the walking subject with exposed controls.");
    window.clickStewardCreateCameraEffect();
    window.clickStewardCreateCameraEffect();
    const std::string inspector = window.inspectorContents();
    const std::string logText = window.logContents();
    const std::string steward = window.stewardContents();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "effectGraphs=" << viewModel.value().timeline.effectGraphs.size() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "log=" << logText << '\n';
    std::cout << "steward=" << steward << '\n';
    const auto& snapshots = workspace.value().project().packageState().snapshots.records();
    const bool intentRecorded = !snapshots.empty() &&
                                snapshots.back().label.has_value() &&
                                snapshots.back().label.value() == "Center the walking subject with exposed controls.";
    const bool cameraHasEffect = std::any_of(
      viewModel.value().timeline.effectGraphs.begin(),
      viewModel.value().timeline.effectGraphs.end(),
      [](const grapple::app::AppEffectGraphRow& graph) {
        return graph.targetNodeId == grapple::foundation::NodeId{"node_camera_4"} &&
               graph.effects.size() == 1 &&
               graph.effects.front().displayName == "Camera Transform" &&
               graph.effects.front().implementationKind == "builtin";
      }
    );
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           cameraHasEffect &&
           inspector.find("Camera Transform") != std::string::npos &&
           inspector.find("Position X (position_x)=0") != std::string::npos &&
           intentRecorded &&
           steward.find("Center the walking subject with exposed controls. @ rev_6") != std::string::npos &&
           logText.find("steward.camera_transform_exists") != std::string::npos &&
           logText.find("runtime.effect_runtime_missing") == std::string::npos
      ? 0
      : 1;
  }

  if (deleteSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    window.deleteSelectedClip();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    return viewModel.value().timeline.clips.empty() ? 0 : 1;
  }

  if (playbackSmoke) {
    window.startPlayback();
    window.advancePlaybackFrame();
    window.pausePlayback();
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead.value > 0.0 ? 0 : 1;
  }

  if (openPackageSmoke) {
    const auto write = workspace.value().project().writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }
    window.openPackageRoot(grapple::foundation::FilePath{"/tmp/grapple-desktop-package"});
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "commands=" << workspace.value().project().packageState().commandLog.records().size() << '\n';
    return viewModel.value().project.projectId == grapple::foundation::ProjectId{"proj_desktop"} &&
           viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_5"} &&
           workspace.value().project().packageState().commandLog.records().size() == 5
      ? 0
      : 1;
  }

  if (editSaveSmoke) {
    window.importVideoFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/walking-woman.avi"});
    window.addSelectedVideoToTimeline();
    const auto write = workspace.value().project().writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }
    auto reopened = grapple::app::NativeWorkspaceSession::openPackageRoot(grapple::foundation::FilePath{"/tmp/grapple-desktop-package"});
    if (!reopened) {
      printError(reopened.error());
      return 1;
    }
    const auto viewModel = reopened.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "commands=" << reopened.value().project().packageState().commandLog.records().size() << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           viewModel.value().assets.count == 2 &&
           viewModel.value().timeline.clips.size() == 2 &&
           reopened.value().project().packageState().commandLog.records().size() == 7
      ? 0
      : 1;
  }

  if (screenshotPath.has_value()) {
    window.show();
    app.processEvents();
    const QPixmap pixmap = window.grab();
    if (!pixmap.save(QString::fromStdString(*screenshotPath))) {
      std::cerr << "Could not write screenshot: " << *screenshotPath << '\n';
      return 1;
    }
    return 0;
  }

  window.show();
  return app.exec();
}
