#include "DesktopApp.hpp"
#include "DesktopWindow.hpp"

#include <DemoProject.hpp>

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/render/RenderQuality.hpp>
#include <grapple/effects/BuiltinEffects.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <QApplication>
#include <QPixmap>
#include <QString>

#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

grapple::foundation::Result<grapple::foundation::Resolution> encodedVideoResolution(
  const std::filesystem::path& path
) {
  cv::VideoCapture capture{path.string()};
  if (!capture.isOpened()) {
    return grapple::foundation::Error{
      "desktop.export_video_open_failed",
      "Could not open exported video for inspection."
    };
  }

  const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
  const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  if (width <= 0 || height <= 0) {
    return grapple::foundation::Error{
      "desktop.export_video_resolution_invalid",
      "Exported video reported an invalid encoded resolution."
    };
  }

  return grapple::foundation::Resolution{width, height};
}

grapple::foundation::Result<void> writeTinyPpm(const std::filesystem::path& path) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if (!output) {
    return grapple::foundation::Error{"desktop.test_image_write_failed", "Could not write test image."};
  }
  output << "P6\n2 1\n255\n";
  const unsigned char pixels[] = {
    240, 20, 10,
    10, 20, 240
  };
  output.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
  return {};
}

grapple::foundation::Result<void> writeDummyAudioFile(const std::filesystem::path& path) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  if (!output) {
    return grapple::foundation::Error{"desktop.test_audio_write_failed", "Could not write test audio file."};
  }
  const auto writeU16 = [&](std::uint16_t value) {
    const unsigned char bytes[] = {
      static_cast<unsigned char>(value & 0xffU),
      static_cast<unsigned char>((value >> 8U) & 0xffU)
    };
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
  };
  const auto writeU32 = [&](std::uint32_t value) {
    const unsigned char bytes[] = {
      static_cast<unsigned char>(value & 0xffU),
      static_cast<unsigned char>((value >> 8U) & 0xffU),
      static_cast<unsigned char>((value >> 16U) & 0xffU),
      static_cast<unsigned char>((value >> 24U) & 0xffU)
    };
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
  };

  constexpr std::uint16_t channels = 1;
  constexpr std::uint32_t sampleRate = 8000;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint32_t dataBytes = sampleRate * channels * (bitsPerSample / 8U);
  output.write("RIFF", 4);
  writeU32(36U + dataBytes);
  output.write("WAVE", 4);
  output.write("fmt ", 4);
  writeU32(16U);
  writeU16(1U);
  writeU16(channels);
  writeU32(sampleRate);
  writeU32(sampleRate * channels * (bitsPerSample / 8U));
  writeU16(channels * (bitsPerSample / 8U));
  writeU16(bitsPerSample);
  output.write("data", 4);
  writeU32(dataBytes);
  for (std::uint32_t index = 0; index < dataBytes / 2U; ++index) {
    writeU16(0U);
  }
  return {};
}

grapple::foundation::Result<void> populateDemo(grapple::app::NativeProjectSession& session, bool savePackage) {
  return grapple::demo::populateStarterDemo(
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

grapple::project::CommandSource desktopUserSource() {
  return grapple::project::CommandSource{
    grapple::project::CommandSourceKind::User,
    std::nullopt,
    "desktop"
  };
}

std::filesystem::path uniqueDesktopSmokeRoot() {
  return std::filesystem::temp_directory_path() /
         ("grapple-desktop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

std::filesystem::path defaultInteractivePackageRoot() {
  const char* home = std::getenv("HOME");
  const std::filesystem::path base = home == nullptr || std::string{home}.empty()
    ? std::filesystem::temp_directory_path() / "Grapple"
    : std::filesystem::path{home} / "Documents" / "Grapple";
  const std::filesystem::path first = base / "Untitled";
  std::error_code existsError;
  if (!std::filesystem::exists(first, existsError)) {
    return first;
  }

  for (int index = 2; index < 1000; ++index) {
    const std::filesystem::path candidate = base / ("Untitled " + std::to_string(index));
    existsError.clear();
    if (!std::filesystem::exists(candidate, existsError)) {
      return candidate;
    }
  }

  return base / ("Untitled " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

} // namespace

int grapple::desktop::runDesktopApp(int argc, char* argv[]) {
  bool smoke = false;
  bool mutateSmoke = false;
  bool seekSmoke = false;
  bool timelineSeekSmoke = false;
  bool selectSmoke = false;
  bool selectAudioClipSmoke = false;
  bool selectAudioTrackSmoke = false;
  bool selectCameraSmoke = false;
  bool selectSecondCameraSmoke = false;
  bool stewardSmoke = false;
  bool importSmoke = false;
  bool importMediaTypesSmoke = false;
  bool addVideoSmoke = false;
  bool emptyAddVideoSmoke = false;
  bool emptyAddVideoUndoSmoke = false;
  bool emptyAddTextSmoke = false;
  bool emptyAddTrackSmoke = false;
  bool emptyAddCameraSmoke = false;
  bool updateCameraSmoke = false;
  bool addNoteSmoke = false;
  bool updateNoteSmoke = false;
  bool moveClipSmoke = false;
  bool trimClipSmoke = false;
  bool clipTimingPanelSmoke = false;
  bool nudgeClipSmoke = false;
  bool undoRedoSmoke = false;
  bool addEffectSmoke = false;
  bool clipEffectControlsSmoke = false;
  bool stewardSubmitShortcutSmoke = false;
  bool stewardTextClipSmoke = false;
  bool stewardNoteSmoke = false;
  bool stewardSuggestionSmoke = false;
  bool setEffectParamSmoke = false;
  bool effectKeyframeSmoke = false;
  bool stewardMotionSmoke = false;
  bool stewardZoomMotionSmoke = false;
  bool stewardClipTransformSmoke = false;
  bool stewardUndoSmoke = false;
  bool stewardUpdateCameraSmoke = false;
  bool stewardCreateTrackSmoke = false;
  bool stewardDeleteTrackSmoke = false;
  bool stewardDeleteClipSmoke = false;
  bool stewardDeleteCameraControlsSmoke = false;
  bool deleteEffectSmoke = false;
  bool deleteSmoke = false;
  bool deleteTrackSmoke = false;
  bool playbackSmoke = false;
  bool openPackageSmoke = false;
  bool editSaveSmoke = false;
  bool newPackageSmoke = false;
  bool exportSettingsSmoke = false;
  bool productLoopSmoke = false;
  bool emptyLaunchSmoke = false;
  bool sampleStartSmoke = false;
  bool emptySaveSmoke = false;
  std::optional<std::string> openPackageRootArg;
  std::optional<std::string> newPackageRootArg;
  std::optional<std::string> screenshotPath;
  std::optional<std::string> effectScreenshotPath;
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
    } else if (argument == "--select-audio-clip-smoke") {
      selectAudioClipSmoke = true;
    } else if (argument == "--select-audio-track-smoke") {
      selectAudioTrackSmoke = true;
    } else if (argument == "--select-camera-smoke") {
      selectCameraSmoke = true;
    } else if (argument == "--select-second-camera-smoke") {
      selectSecondCameraSmoke = true;
    } else if (argument == "--steward-smoke") {
      stewardSmoke = true;
    } else if (argument == "--import-smoke") {
      importSmoke = true;
    } else if (argument == "--import-media-types-smoke") {
      importMediaTypesSmoke = true;
    } else if (argument == "--add-video-smoke") {
      addVideoSmoke = true;
    } else if (argument == "--empty-add-video-smoke") {
      emptyAddVideoSmoke = true;
    } else if (argument == "--empty-add-video-undo-smoke") {
      emptyAddVideoUndoSmoke = true;
    } else if (argument == "--empty-add-text-smoke") {
      emptyAddTextSmoke = true;
    } else if (argument == "--empty-add-track-smoke") {
      emptyAddTrackSmoke = true;
    } else if (argument == "--empty-add-camera-smoke") {
      emptyAddCameraSmoke = true;
    } else if (argument == "--update-camera-smoke") {
      updateCameraSmoke = true;
    } else if (argument == "--add-note-smoke") {
      addNoteSmoke = true;
    } else if (argument == "--update-note-smoke") {
      updateNoteSmoke = true;
    } else if (argument == "--move-clip-smoke") {
      moveClipSmoke = true;
    } else if (argument == "--trim-clip-smoke") {
      trimClipSmoke = true;
    } else if (argument == "--clip-timing-panel-smoke") {
      clipTimingPanelSmoke = true;
    } else if (argument == "--nudge-clip-smoke") {
      nudgeClipSmoke = true;
    } else if (argument == "--undo-redo-smoke") {
      undoRedoSmoke = true;
    } else if (argument == "--add-effect-smoke") {
      addEffectSmoke = true;
    } else if (argument == "--clip-effect-controls-smoke") {
      clipEffectControlsSmoke = true;
    } else if (argument == "--steward-submit-shortcut-smoke") {
      stewardSubmitShortcutSmoke = true;
    } else if (argument == "--steward-text-clip-smoke") {
      stewardTextClipSmoke = true;
    } else if (argument == "--steward-note-smoke") {
      stewardNoteSmoke = true;
    } else if (argument == "--steward-suggestion-smoke") {
      stewardSuggestionSmoke = true;
    } else if (argument == "--set-effect-param-smoke") {
      setEffectParamSmoke = true;
    } else if (argument == "--effect-keyframe-smoke") {
      effectKeyframeSmoke = true;
    } else if (argument == "--steward-motion-smoke") {
      stewardMotionSmoke = true;
    } else if (argument == "--steward-zoom-motion-smoke") {
      stewardZoomMotionSmoke = true;
    } else if (argument == "--steward-clip-transform-smoke") {
      stewardClipTransformSmoke = true;
    } else if (argument == "--steward-undo-smoke") {
      stewardUndoSmoke = true;
    } else if (argument == "--steward-update-camera-smoke") {
      stewardUpdateCameraSmoke = true;
    } else if (argument == "--steward-create-track-smoke") {
      stewardCreateTrackSmoke = true;
    } else if (argument == "--steward-delete-track-smoke") {
      stewardDeleteTrackSmoke = true;
    } else if (argument == "--steward-delete-clip-smoke") {
      stewardDeleteClipSmoke = true;
    } else if (argument == "--steward-delete-camera-controls-smoke") {
      stewardDeleteCameraControlsSmoke = true;
    } else if (argument == "--delete-effect-smoke") {
      deleteEffectSmoke = true;
    } else if (argument == "--delete-smoke") {
      deleteSmoke = true;
    } else if (argument == "--delete-track-smoke") {
      deleteTrackSmoke = true;
    } else if (argument == "--playback-smoke") {
      playbackSmoke = true;
    } else if (argument == "--open-package-smoke") {
      openPackageSmoke = true;
    } else if (argument == "--edit-save-smoke") {
      editSaveSmoke = true;
    } else if (argument == "--new-package-smoke") {
      newPackageSmoke = true;
    } else if (argument == "--export-settings-smoke") {
      exportSettingsSmoke = true;
    } else if (argument == "--product-loop-smoke") {
      productLoopSmoke = true;
    } else if (argument == "--empty-launch-smoke") {
      emptyLaunchSmoke = true;
    } else if (argument == "--sample-start-smoke") {
      sampleStartSmoke = true;
    } else if (argument == "--empty-save-smoke") {
      emptySaveSmoke = true;
    } else if (argument == "--open-package" && index + 1 < argc) {
      openPackageRootArg = argv[++index];
    } else if (argument == "--new-package" && index + 1 < argc) {
      newPackageRootArg = argv[++index];
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else if (argument == "--effect-screenshot" && index + 1 < argc) {
      effectScreenshotPath = argv[++index];
    } else {
      std::cerr << "Expected --smoke, --mutate-smoke, --seek-smoke, --timeline-seek-smoke, --select-smoke, --select-audio-clip-smoke, --select-audio-track-smoke, --select-camera-smoke, --select-second-camera-smoke, --steward-smoke, --import-smoke, --import-media-types-smoke, --add-video-smoke, --empty-add-video-smoke, --empty-add-video-undo-smoke, --empty-add-text-smoke, --empty-add-track-smoke, --empty-add-camera-smoke, --update-camera-smoke, --add-note-smoke, --update-note-smoke, --move-clip-smoke, --trim-clip-smoke, --clip-timing-panel-smoke, --nudge-clip-smoke, --undo-redo-smoke, --add-effect-smoke, --clip-effect-controls-smoke, --steward-submit-shortcut-smoke, --steward-text-clip-smoke, --steward-note-smoke, --steward-suggestion-smoke, --set-effect-param-smoke, --effect-keyframe-smoke, --steward-motion-smoke, --steward-zoom-motion-smoke, --steward-clip-transform-smoke, --steward-undo-smoke, --steward-delete-track-smoke, --steward-delete-clip-smoke, --steward-delete-camera-controls-smoke, --delete-effect-smoke, --delete-smoke, --delete-track-smoke, --playback-smoke, --open-package-smoke, --edit-save-smoke, --new-package-smoke, --export-settings-smoke, --product-loop-smoke, --empty-launch-smoke, --sample-start-smoke, --empty-save-smoke, --open-package <path>, --new-package <path>, --screenshot <path>, or --effect-screenshot <path>.\n";
      return 1;
    }
  }
  if (openPackageRootArg.has_value() && newPackageRootArg.has_value()) {
    std::cerr << "--open-package and --new-package are mutually exclusive.\n";
    return 1;
  }

  QApplication app{argc, argv};
  const std::filesystem::path smokeRoot = uniqueDesktopSmokeRoot();
  std::error_code smokeRootError;
  std::filesystem::create_directories(smokeRoot, smokeRootError);
  if (smokeRootError) {
    printError(grapple::foundation::Error{"desktop.smoke_root_create_failed", "Could not create desktop smoke root."});
    return 1;
  }
  const std::filesystem::path packageRoot = smokeRoot / "package";
  const std::filesystem::path starterVideoPath{"/tmp/grapple-native-demo/starter-gradient.avi"};

  const bool populateStarterDemo =
    smoke ||
    mutateSmoke ||
    seekSmoke ||
    timelineSeekSmoke ||
    selectSmoke ||
    selectAudioClipSmoke ||
    selectAudioTrackSmoke ||
    selectCameraSmoke ||
    selectSecondCameraSmoke ||
    stewardSmoke ||
    importSmoke ||
    addVideoSmoke ||
    updateCameraSmoke ||
    addNoteSmoke ||
    updateNoteSmoke ||
    moveClipSmoke ||
    trimClipSmoke ||
    clipTimingPanelSmoke ||
    nudgeClipSmoke ||
    undoRedoSmoke ||
    addEffectSmoke ||
    clipEffectControlsSmoke ||
    stewardSubmitShortcutSmoke ||
    stewardTextClipSmoke ||
    setEffectParamSmoke ||
    effectKeyframeSmoke ||
    stewardMotionSmoke ||
    stewardZoomMotionSmoke ||
    stewardUndoSmoke ||
    stewardUpdateCameraSmoke ||
    stewardCreateTrackSmoke ||
    stewardDeleteTrackSmoke ||
    deleteEffectSmoke ||
    deleteSmoke ||
    deleteTrackSmoke ||
    playbackSmoke ||
    openPackageSmoke ||
    editSaveSmoke ||
    exportSettingsSmoke ||
    effectScreenshotPath.has_value();
  const bool needsStarterDemoVideo =
    populateStarterDemo ||
    emptyAddVideoSmoke ||
    emptyAddVideoUndoSmoke ||
    sampleStartSmoke ||
    stewardSuggestionSmoke ||
    productLoopSmoke ||
    stewardDeleteClipSmoke ||
    stewardDeleteCameraControlsSmoke ||
    stewardUndoSmoke ||
    stewardClipTransformSmoke;

  if (needsStarterDemoVideo) {
    const auto demoVideo = grapple::demo::ensureStarterDemoVideo();
    if (!demoVideo) {
      printError(demoVideo.error());
      return 1;
    }
  }

  auto workspace = [&]() -> grapple::foundation::Result<grapple::app::NativeWorkspaceSession> {
    if (openPackageRootArg.has_value()) {
      return grapple::app::NativeWorkspaceSession::openPackageRoot(
        grapple::foundation::FilePath{*openPackageRootArg}
      );
    }
    if (newPackageRootArg.has_value()) {
      auto created = grapple::app::NativeWorkspaceSession::createPackageRoot(
        grapple::foundation::FilePath{*newPackageRootArg},
        std::filesystem::path{*newPackageRootArg}.filename().string()
      );
      if (!created) {
        return created.error();
      }
      auto write = created.value().writePackage();
      if (!write) {
        return write.error();
      }
      return std::move(created.value());
    }

    if (!populateStarterDemo) {
      const std::filesystem::path defaultPackageRoot = argc == 1
        ? defaultInteractivePackageRoot()
        : packageRoot;
      const std::string defaultProjectName = argc == 1
        ? defaultPackageRoot.filename().string()
        : std::string{"Untitled"};
      auto created = grapple::app::NativeWorkspaceSession::createPackageRoot(
        grapple::foundation::FilePath{defaultPackageRoot.string()},
        defaultProjectName
      );
      if (!created) {
        return created.error();
      }
      auto write = created.value().writePackage();
      if (!write) {
        return write.error();
      }
      return std::move(created.value());
    }

    grapple::app::NativeProjectSession session{
      grapple::foundation::ProjectId{"proj_desktop"},
      "Desktop Demo",
      grapple::storage::ProjectPackage{
        grapple::foundation::ProjectId{"proj_desktop"},
        grapple::foundation::FilePath{packageRoot.string()},
        grapple::storage::CurrentProjectPackageSchemaVersion
      }
    };

    if (populateStarterDemo) {
      const auto populated = populateDemo(session, true);
      if (!populated) {
        return populated.error();
      }
    }
    return grapple::app::NativeWorkspaceSession::fromProject(std::move(session));
  }();

  if (!workspace) {
    printError(workspace.error());
    return 1;
  }

  DesktopWindow window{workspace.value()};

  if (emptyLaunchSmoke) {
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string steward = window.stewardContents();
    const std::string timelineEmptyPrompt = window.timelineEmptyPromptText();
    const std::string stewardIntent = window.stewardIntent();
    const std::string stewardIntentPlaceholder = window.stewardIntentPlaceholder();
    const std::string stewardActionText = window.stewardPrimaryActionText();
    const bool stewardActionEnabled = window.stewardPrimaryActionEnabled();
    const bool exportActionEnabled = window.exportActionEnabled();
    const bool saveActionEnabled = window.saveActionEnabled();
    const bool undoActionEnabled = window.undoActionEnabled();
    const bool redoActionEnabled = window.redoActionEnabled();
    const bool playActionEnabled = window.playActionEnabled();
    const bool pauseActionEnabled = window.pauseActionEnabled();
    const bool seekActionEnabled = window.seekActionEnabled();
    const bool selectedClipMenuActionsEnabled = window.selectedClipMenuActionsEnabled();
    const bool selectedTrackMenuActionEnabled = window.selectedTrackMenuActionEnabled();
    const bool selectedNoteMenuActionEnabled = window.selectedNoteMenuActionEnabled();
    const std::filesystem::path currentPackageRoot{
      workspace.value().project().packageState().package.rootPath.value
    };
    const bool packageWritten =
      std::filesystem::exists(currentPackageRoot / "manifest.json") &&
      std::filesystem::exists(currentPackageRoot / "agent" / "runs.json") &&
      std::filesystem::exists(currentPackageRoot / "agent" / "events.json");
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    std::cout << "packageWritten=" << (packageWritten ? "true" : "false") << '\n';
    std::cout << "timelineEmptyPrompt=" << timelineEmptyPrompt << '\n';
    std::cout << "stewardIntent=" << stewardIntent << '\n';
    std::cout << "stewardIntentPlaceholder=" << stewardIntentPlaceholder << '\n';
    std::cout << "stewardAction=" << stewardActionText << '\n';
    std::cout << "stewardActionEnabled=" << (stewardActionEnabled ? "true" : "false") << '\n';
    std::cout << "exportActionEnabled=" << (exportActionEnabled ? "true" : "false") << '\n';
    std::cout << "saveActionEnabled=" << (saveActionEnabled ? "true" : "false") << '\n';
    std::cout << "undoActionEnabled=" << (undoActionEnabled ? "true" : "false") << '\n';
    std::cout << "redoActionEnabled=" << (redoActionEnabled ? "true" : "false") << '\n';
    std::cout << "playActionEnabled=" << (playActionEnabled ? "true" : "false") << '\n';
    std::cout << "pauseActionEnabled=" << (pauseActionEnabled ? "true" : "false") << '\n';
    std::cout << "seekActionEnabled=" << (seekActionEnabled ? "true" : "false") << '\n';
    std::cout << "selectedClipMenuActionsEnabled=" << (selectedClipMenuActionsEnabled ? "true" : "false") << '\n';
    std::cout << "selectedTrackMenuActionEnabled=" << (selectedTrackMenuActionEnabled ? "true" : "false") << '\n';
    std::cout << "selectedNoteMenuActionEnabled=" << (selectedNoteMenuActionEnabled ? "true" : "false") << '\n';
    std::cout << "steward=" << steward << '\n';
    return viewModel.value().assets.count == 0 &&
           viewModel.value().timeline.clips.empty() &&
           viewModel.value().timeline.cameras.empty() &&
           packageWritten &&
           stewardActionText == "Start Sample" &&
           stewardActionEnabled &&
           !exportActionEnabled &&
           !saveActionEnabled &&
           !undoActionEnabled &&
           !redoActionEnabled &&
           !playActionEnabled &&
           !pauseActionEnabled &&
           !seekActionEnabled &&
           !selectedClipMenuActionsEnabled &&
           !selectedTrackMenuActionEnabled &&
           !selectedNoteMenuActionEnabled &&
           stewardIntent.empty() &&
           timelineEmptyPrompt.find("Use Sample to start now") != std::string::npos &&
           timelineEmptyPrompt.find("import/drop media, then double-click it") != std::string::npos &&
           timelineEmptyPrompt.find("Ask Steward for an editable change") != std::string::npos &&
           stewardIntentPlaceholder.find("Start Sample") != std::string::npos &&
           stewardIntentPlaceholder.find("import/drop your own media") != std::string::npos &&
           steward.find("0 assets | 0 clips | 0 cameras | 0 editable effects") != std::string::npos &&
           steward.find("Next: start the sample, then try one suggested editable request.") != std::string::npos
      ? 0
      : 1;
  }

  if (sampleStartSmoke) {
    const std::string stewardActionBeforeStart = window.stewardPrimaryActionText();
    const bool stewardActionEnabledBeforeStart = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto previewFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!previewFrame) {
      printError(previewFrame.error());
      return 1;
    }
    const auto selectedNode = window.selectedNodeId();
    const std::string steward = window.stewardContents();
    const std::string log = window.logContents();
    const std::string detailTab = window.currentDetailTabText();
    const std::string stewardAction = window.stewardPrimaryActionText();
    const bool stewardActionEnabled = window.stewardPrimaryActionEnabled();
    const bool playActionEnabled = window.playActionEnabled();
    const bool exportActionEnabled = window.exportActionEnabled();
    const bool saveActionEnabled = window.saveActionEnabled();
    const bool undoActionEnabled = window.undoActionEnabled();
    const bool addMediaActionEnabled = window.addSelectedMediaActionEnabled();
    const bool selectedClip =
      selectedNode.has_value() &&
      !viewModel.value().timeline.clips.empty() &&
      selectedNode.value() == viewModel.value().timeline.clips.front().sourceNodeId;
    const bool currentPreview =
      previewFrame.value().frame.sourceRevision == viewModel.value().project.revision &&
      previewFrame.value().frame.mediaFrames.size() == 1 &&
      previewFrame.value().frame.cameras.size() == 1 &&
      previewFrame.value().runtimeDiagnostics.empty() &&
      previewFrame.value().renderDiagnostics.empty();

    std::cout << "stewardActionBeforeStart=" << stewardActionBeforeStart << '\n';
    std::cout << "stewardActionEnabledBeforeStart=" << (stewardActionEnabledBeforeStart ? "true" : "false") << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    std::cout << "selectedNode=" << (selectedNode.has_value() ? selectedNode->value() : "none") << '\n';
    std::cout << "selectedClip=" << (selectedClip ? "true" : "false") << '\n';
    std::cout << "detailTab=" << detailTab << '\n';
    std::cout << "currentPreview=" << (currentPreview ? "true" : "false") << '\n';
    std::cout << "mediaFrames=" << previewFrame.value().frame.mediaFrames.size() << '\n';
    std::cout << "camerasInFrame=" << previewFrame.value().frame.cameras.size() << '\n';
    std::cout << "stewardAction=" << stewardAction << '\n';
    std::cout << "stewardActionEnabled=" << (stewardActionEnabled ? "true" : "false") << '\n';
    std::cout << "playActionEnabled=" << (playActionEnabled ? "true" : "false") << '\n';
    std::cout << "exportActionEnabled=" << (exportActionEnabled ? "true" : "false") << '\n';
    std::cout << "saveActionEnabled=" << (saveActionEnabled ? "true" : "false") << '\n';
    std::cout << "undoActionEnabled=" << (undoActionEnabled ? "true" : "false") << '\n';
    std::cout << "addMediaActionEnabled=" << (addMediaActionEnabled ? "true" : "false") << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "log=" << log << '\n';

    return viewModel.value().assets.count == 1 &&
           viewModel.value().timeline.clips.size() == 1 &&
           viewModel.value().timeline.cameras.size() == 1 &&
           stewardActionBeforeStart == "Start Sample" &&
           stewardActionEnabledBeforeStart &&
           selectedClip &&
           detailTab == "Clip" &&
           currentPreview &&
           stewardAction == "Choose Or Type Request" &&
           !stewardActionEnabled &&
           playActionEnabled &&
           exportActionEnabled &&
           saveActionEnabled &&
           undoActionEnabled &&
           !addMediaActionEnabled &&
           steward.find("Project: 1 assets | 1 clips | 1 cameras | 0 editable effects") != std::string::npos &&
           steward.find("Added selected media to the timeline.") != std::string::npos &&
           steward.find("Place Asset On Timeline -> succeeded at ") != std::string::npos &&
           log.find("Imported starter-gradient") != std::string::npos &&
           log.find("Steward added selected media to timeline") != std::string::npos
      ? 0
      : 1;
  }

  if (emptySaveSmoke) {
    const auto write = workspace.value().writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }
    auto reopened = grapple::app::NativeWorkspaceSession::openPackageRoot(
      grapple::foundation::FilePath{packageRoot.string()}
    );
    if (!reopened) {
      printError(reopened.error());
      return 1;
    }
    const auto viewModel = reopened.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
    std::cout << "name=" << viewModel.value().project.name << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "commands=" << reopened.value().project().packageState().commandLog.records().size() << '\n';
    std::cout << "snapshots=" << reopened.value().project().packageState().snapshots.records().size() << '\n';
    return viewModel.value().project.projectId == grapple::foundation::ProjectId{"proj_package"} &&
           viewModel.value().project.name == "Untitled" &&
           viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_0"} &&
           reopened.value().project().packageState().commandLog.records().empty() &&
           reopened.value().project().packageState().snapshots.records().size() == 1
      ? 0
      : 1;
  }

  if (emptyAddTextSmoke) {
    window.addTextClip();
    if (window.currentDetailTabText() != "Text") {
      std::cerr << "Text clip selection did not open text controls.\n";
      return 1;
    }
    window.setSelectedTextClipTextControlValue("MVP Title");
    window.setSelectedTextClipPropertyControlValue("textClipFontSize", 72.0);
    window.setSelectedTextClipPropertyControlValue("textClipOpacity", 0.5);
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.textClips.empty()) {
      std::cerr << "No text clips.\n";
      return 1;
    }

    const grapple::app::AppTextClipRow& clip = viewModel.value().timeline.textClips.front();
    std::cout << "text=" << clip.text << '\n';
    std::cout << "fontSize=" << clip.style.fontSize << '\n';
    std::cout << "opacity=" << clip.transform.opacity << '\n';
    std::cout << "tab=" << window.currentDetailTabText() << '\n';
    return viewModel.value().timeline.layers.size() == 1 &&
           viewModel.value().timeline.textClips.size() == 1 &&
           clip.text == "MVP Title" &&
           clip.style.fontSize == 72.0 &&
           clip.transform.opacity == 0.5 &&
           window.currentDetailTabText() == "Text"
      ? 0
      : 1;
  }

  if (stewardTextClipSmoke) {
    window.setStewardIntent("Add title \"Opening Title\".");
    const std::string createTextPrimaryActionText = window.stewardPrimaryActionText();
    const bool createTextPrimaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    if (window.currentDetailTabText() != "Text") {
      std::cerr << "Created Steward text clip was not selected.\n";
      return 1;
    }
    window.setStewardIntent("Change title to \"Final Title\", make font smaller, move text right and up, make it shorter, and fade it.");
    const std::string updateTextPrimaryActionText = window.stewardPrimaryActionText();
    const bool updateTextPrimaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.textClips.empty()) {
      std::cerr << "No Steward text clip was created.\n";
      return 1;
    }

    const grapple::app::AppTextClipRow& clip = viewModel.value().timeline.textClips.back();
    const auto conversation = workspace.value().steward().conversationState();
    std::cout << "text=" << clip.text << '\n';
    std::cout << "fontSize=" << clip.style.fontSize << '\n';
    std::cout << "positionX=" << clip.transform.position.x << '\n';
    std::cout << "positionY=" << clip.transform.position.y << '\n';
    std::cout << "opacity=" << clip.transform.opacity << '\n';
    std::cout << "end=" << clip.timelineRange.end.value << '\n';
    std::cout << "tab=" << window.currentDetailTabText() << '\n';
    std::cout << "createTextPrimaryAction=" << createTextPrimaryActionText << '\n';
    std::cout << "createTextPrimaryActionEnabled=" << (createTextPrimaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "updateTextPrimaryAction=" << updateTextPrimaryActionText << '\n';
    std::cout << "updateTextPrimaryActionEnabled=" << (updateTextPrimaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    return viewModel.value().timeline.textClips.size() == 1 &&
           clip.text == "Final Title" &&
           clip.style.fontSize == 48.0 &&
           clip.transform.position.x == 0.25 &&
           clip.transform.position.y == 0.55 &&
           clip.transform.opacity == 0.5 &&
           clip.timelineRange.end == grapple::foundation::TimeSeconds{2.0} &&
           window.currentDetailTabText() == "Text" &&
           createTextPrimaryActionText == "Create Text Clip" &&
           createTextPrimaryActionEnabled &&
           updateTextPrimaryActionText == "Apply Request To Text" &&
           updateTextPrimaryActionEnabled &&
           window.stewardIntent().empty() &&
           conversation.runs.size() == 2 &&
           conversation.runs[0].toolCalls.size() == 1 &&
           conversation.runs[0].toolCalls[0].toolSerializedId == "timeline.create_text_clip" &&
           conversation.runs[1].toolCalls.size() == 1 &&
           conversation.runs[1].toolCalls[0].toolSerializedId == "timeline.update_text_clip"
      ? 0
      : 1;
  }

  if (stewardNoteSmoke) {
    window.setStewardIntent("Add note \"Camera rationale\" saying Keep zoom editable.");
    const std::string createNotePrimaryActionText = window.stewardPrimaryActionText();
    const bool createNotePrimaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    if (window.currentDetailTabText() != "Inspector") {
      std::cerr << "Created Steward note was not selected.\n";
      return 1;
    }
    window.setStewardIntent("Update note to \"Keep zoom exposed as a user-editable control.\"");
    const std::string updateNotePrimaryActionText = window.stewardPrimaryActionText();
    const bool updateNotePrimaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().notes.rows.empty()) {
      std::cerr << "No Steward note was created.\n";
      return 1;
    }

    const grapple::app::AppNoteRow& note = viewModel.value().notes.rows.back();
    const auto conversation = workspace.value().steward().conversationState();
    const std::string inspector = window.inspectorContents();
    std::cout << "title=" << note.title << '\n';
    std::cout << "markdown=" << note.markdown << '\n';
    std::cout << "tab=" << window.currentDetailTabText() << '\n';
    std::cout << "createNotePrimaryAction=" << createNotePrimaryActionText << '\n';
    std::cout << "createNotePrimaryActionEnabled=" << (createNotePrimaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "updateNotePrimaryAction=" << updateNotePrimaryActionText << '\n';
    std::cout << "updateNotePrimaryActionEnabled=" << (updateNotePrimaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_2"} &&
           viewModel.value().notes.rows.size() == 1 &&
           note.title == "Camera rationale" &&
           note.markdown == "Keep zoom exposed as a user-editable control." &&
           window.currentDetailTabText() == "Inspector" &&
           createNotePrimaryActionText == "Create Note" &&
           createNotePrimaryActionEnabled &&
           updateNotePrimaryActionText == "Apply Request To Note" &&
           updateNotePrimaryActionEnabled &&
           window.selectedNoteMenuActionEnabled() &&
           window.stewardIntent().empty() &&
           inspector.find("Inspector\nNote\nCamera rationale") != std::string::npos &&
           conversation.runs.size() == 2 &&
           conversation.runs[0].toolCalls.size() == 1 &&
           conversation.runs[0].toolCalls[0].toolSerializedId == "note.create" &&
           conversation.runs[1].toolCalls.size() == 1 &&
           conversation.runs[1].toolCalls[0].toolSerializedId == "note.update"
      ? 0
      : 1;
  }

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
    const std::string log = window.logContents();
    const bool selectedClipMenuActionsEnabled = window.selectedClipMenuActionsEnabled();
    const bool selectedTrackMenuActionEnabled = window.selectedTrackMenuActionEnabled();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "selected=" << selectedNodeId->value() << '\n';
    std::cout << "runs=" << workspace.value().steward().conversationState().runs.size() << '\n';
    std::cout << "selectedClipMenuActionsEnabled=" << (selectedClipMenuActionsEnabled ? "true" : "false") << '\n';
    std::cout << "selectedTrackMenuActionEnabled=" << (selectedTrackMenuActionEnabled ? "true" : "false") << '\n';
    return !viewModel.value().timeline.clips.empty() &&
           selectedNodeId.value() == viewModel.value().timeline.clips.front().sourceNodeId &&
           selectedClipMenuActionsEnabled &&
           !selectedTrackMenuActionEnabled &&
           workspace.value().steward().conversationState().runs.empty() &&
           viewModel.value().timeline.effectCount == 0 &&
           log.find("steward.selected_node_not_camera") == std::string::npos
      ? 0
      : 1;
  }

  if (selectAudioClipSmoke) {
    const std::filesystem::path audioPath = smokeRoot / "select-audio.wav";
    const auto audioWrite = writeDummyAudioFile(audioPath);
    if (!audioWrite) {
      printError(audioWrite.error());
      return 1;
    }

    window.importMediaFile(grapple::foundation::FilePath{audioPath.string()});
    window.addSelectedMediaToTimeline();
    window.show();
    app.processEvents();
    window.clickFirstTimelineAudioClip();

    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      std::filesystem::remove(audioPath);
      return 1;
    }
    if (viewModel.value().timeline.audioClips.empty()) {
      std::cerr << "No audio clips.\n";
      std::filesystem::remove(audioPath);
      return 1;
    }

    const std::string inspector = window.inspectorContents();
    const auto selectedNodeId = window.selectedNodeId();
    std::cout << "inspector=" << inspector << '\n';
    if (selectedNodeId.has_value()) {
      std::cout << "selected=" << selectedNodeId->value() << '\n';
    }
    std::filesystem::remove(audioPath);
    return selectedNodeId.has_value() &&
           selectedNodeId.value() == viewModel.value().timeline.audioClips.front().sourceNodeId &&
           inspector.find("Inspector\nClip") != std::string::npos &&
           inspector.find("Type: audio") != std::string::npos
      ? 0
      : 1;
  }

  if (selectAudioTrackSmoke) {
    const std::filesystem::path audioPath = smokeRoot / "select-audio-track.wav";
    const auto audioWrite = writeDummyAudioFile(audioPath);
    if (!audioWrite) {
      printError(audioWrite.error());
      return 1;
    }

    window.importMediaFile(grapple::foundation::FilePath{audioPath.string()});
    window.addSelectedMediaToTimeline();
    window.show();
    app.processEvents();
    window.clickFirstTimelineAudioTrack();

    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      std::filesystem::remove(audioPath);
      return 1;
    }
    if (viewModel.value().timeline.audioTracks.empty()) {
      std::cerr << "No audio tracks.\n";
      std::filesystem::remove(audioPath);
      return 1;
    }

    const std::string inspector = window.inspectorContents();
    const auto selectedNodeId = window.selectedNodeId();
    const bool selectedClipMenuActionsEnabled = window.selectedClipMenuActionsEnabled();
    const bool selectedTrackMenuActionEnabled = window.selectedTrackMenuActionEnabled();
    std::cout << "inspector=" << inspector << '\n';
    if (selectedNodeId.has_value()) {
      std::cout << "selected=" << selectedNodeId->value() << '\n';
    }
    std::cout << "selectedClipMenuActionsEnabled=" << (selectedClipMenuActionsEnabled ? "true" : "false") << '\n';
    std::cout << "selectedTrackMenuActionEnabled=" << (selectedTrackMenuActionEnabled ? "true" : "false") << '\n';
    std::filesystem::remove(audioPath);
    return selectedNodeId.has_value() &&
           selectedNodeId.value() == viewModel.value().timeline.audioTracks.front().sourceNodeId &&
           !selectedClipMenuActionsEnabled &&
           selectedTrackMenuActionEnabled &&
           inspector.find("Inspector\nAudio Track") != std::string::npos &&
           inspector.find("Clips: 1") != std::string::npos
      ? 0
      : 1;
  }

  if (selectCameraSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto selectedNodeId = window.selectedNodeId();
    if (!selectedNodeId.has_value()) {
      std::cerr << "No selected camera node.\n";
      return 1;
    }
    const std::string inspector = window.inspectorContents();
    const std::string log = window.logContents();
    const bool selectedClipMenuActionsEnabled = window.selectedClipMenuActionsEnabled();
    const bool selectedTrackMenuActionEnabled = window.selectedTrackMenuActionEnabled();
    const std::string currentDetailTab = window.currentDetailTabText();
    std::cout << "selected=" << selectedNodeId->value() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "selectedClipMenuActionsEnabled=" << (selectedClipMenuActionsEnabled ? "true" : "false") << '\n';
    std::cout << "selectedTrackMenuActionEnabled=" << (selectedTrackMenuActionEnabled ? "true" : "false") << '\n';
    std::cout << "currentDetailTab=" << currentDetailTab << '\n';
    return !viewModel.value().timeline.cameras.empty() &&
           selectedNodeId.value() == viewModel.value().timeline.cameras.front().sourceNodeId &&
           !selectedClipMenuActionsEnabled &&
           !selectedTrackMenuActionEnabled &&
           currentDetailTab == "Camera" &&
           inspector.find("Camera\nName: Camera") != std::string::npos &&
           inspector.find("No effects attached.") != std::string::npos
      ? 0
      : 1;
  }

  if (selectSecondCameraSmoke) {
    window.addCamera();
    window.show();
    app.processEvents();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.cameras.size() != 2) {
      std::cerr << "Expected two cameras.\n";
      return 1;
    }

    window.clickFirstTimelineCamera();
    const auto firstSelectedNodeId = window.selectedNodeId();
    window.clickSecondTimelineCamera();
    const auto secondSelectedNodeId = window.selectedNodeId();
    if (!firstSelectedNodeId.has_value() || !secondSelectedNodeId.has_value()) {
      std::cerr << "Camera selection missing.\n";
      return 1;
    }

    std::cout << "firstSelected=" << firstSelectedNodeId->value() << '\n';
    std::cout << "secondSelected=" << secondSelectedNodeId->value() << '\n';
    return firstSelectedNodeId.value() == viewModel.value().timeline.cameras[0].sourceNodeId &&
           secondSelectedNodeId.value() == viewModel.value().timeline.cameras[1].sourceNodeId
      ? 0
      : 1;
  }

  if (stewardSmoke) {
    const std::string steward = window.stewardContents();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto selectedNodeId = window.selectedNodeId();
    std::cout << "steward=" << steward << '\n';
    if (selectedNodeId.has_value()) {
      std::cout << "selected=" << selectedNodeId->value() << '\n';
    }
    return steward.find("Current request") == std::string::npos &&
           steward.find("Center the subject with an editable camera transform.") == std::string::npos &&
           steward.find("Loop") == std::string::npos &&
           steward.find("Create an editable result") == std::string::npos &&
           steward.find("editable graph") == std::string::npos &&
           steward.find("Recent runs: none") != std::string::npos &&
           !viewModel.value().timeline.cameras.empty() &&
           selectedNodeId.has_value() &&
           selectedNodeId.value() == viewModel.value().timeline.cameras.front().sourceNodeId
      ? 0
      : 1;
  }

  if (importSmoke) {
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
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

  if (importMediaTypesSmoke) {
    const std::filesystem::path imagePath = smokeRoot / "import-image.ppm";
    const std::filesystem::path audioPath = smokeRoot / "import-audio.wav";
    const auto imageWrite = writeTinyPpm(imagePath);
    if (!imageWrite) {
      printError(imageWrite.error());
      return 1;
    }
    const auto audioWrite = writeDummyAudioFile(audioPath);
    if (!audioWrite) {
      printError(audioWrite.error());
      return 1;
    }

    window.importMediaFiles({
      grapple::foundation::FilePath{imagePath.string()},
      grapple::foundation::FilePath{audioPath.string()}
    });
    window.addMediaAssetAtRowToTimeline(0);
    window.addMediaAssetAtRowToTimeline(1);
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }

    std::size_t imageAssets = 0;
    std::size_t audioAssets = 0;
    for (const grapple::app::AppAssetRow& asset : viewModel.value().assets.rows) {
      if (asset.mediaType == "image") {
        ++imageAssets;
      } else if (asset.mediaType == "audio") {
        ++audioAssets;
      }
    }
    std::size_t imageSources = 0;
    std::size_t audioSources = 0;
    for (const grapple::media::MediaSource& source : workspace.value().mediaSources().sources()) {
      if (source.kind == grapple::media::MediaSourceKind::Image) {
        ++imageSources;
      } else if (source.kind == grapple::media::MediaSourceKind::Audio) {
        ++audioSources;
      }
    }

    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "imageAssets=" << imageAssets << '\n';
    std::cout << "audioAssets=" << audioAssets << '\n';
    std::cout << "imageSources=" << imageSources << '\n';
    std::cout << "audioSources=" << audioSources << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "audioTracks=" << viewModel.value().timeline.audioTracks.size() << '\n';
    std::cout << "audioClips=" << viewModel.value().timeline.audioClips.size() << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';

    std::filesystem::remove(imagePath);
    std::filesystem::remove(audioPath);
    return viewModel.value().assets.count == 2 &&
           imageAssets == 1 &&
           audioAssets == 1 &&
           imageSources == 1 &&
           audioSources == 1 &&
           viewModel.value().timeline.clips.size() == 1 &&
           viewModel.value().timeline.clips.front().kind == "image" &&
           viewModel.value().timeline.audioTracks.size() == 1 &&
           viewModel.value().timeline.audioClips.size() == 1 &&
           viewModel.value().timeline.audioClips.front().kind == "audio" &&
           viewModel.value().timeline.duration.value == 6.0
      ? 0
      : 1;
  }

  if (addVideoSmoke) {
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    window.addMediaAssetAtRowToTimeline(1);
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

  if (emptyAddVideoSmoke) {
    window.show();
    app.processEvents();
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    const std::string stewardActionAfterImport = window.stewardPrimaryActionText();
    const bool stewardActionEnabledAfterImport = window.stewardPrimaryActionEnabled();
    const bool addMediaActionEnabledAfterImport = window.addSelectedMediaActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "compositions=" << viewModel.value().timeline.compositions.size() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
    std::cout << "stewardActionAfterImport=" << stewardActionAfterImport << '\n';
    std::cout << "stewardActionEnabledAfterImport=" << (stewardActionEnabledAfterImport ? "true" : "false") << '\n';
    std::cout << "addMediaActionEnabledAfterImport=" << (addMediaActionEnabledAfterImport ? "true" : "false") << '\n';
    const std::string inspector = window.inspectorContents();
    if (window.selectedNodeId().has_value()) {
      std::cout << "selectedNode=" << window.selectedNodeId()->value() << '\n';
    }
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().assets.count == 1 &&
           viewModel.value().timeline.compositions.size() == 1 &&
           viewModel.value().timeline.layers.size() == 1 &&
           viewModel.value().timeline.cameras.size() == 1 &&
           viewModel.value().timeline.clips.size() == 1 &&
           viewModel.value().timeline.duration.value > 9.9 &&
           stewardActionAfterImport == "Add Selected Media To Timeline" &&
           stewardActionEnabledAfterImport &&
           addMediaActionEnabledAfterImport &&
           window.selectedNodeId().has_value() &&
           window.selectedNodeId().value() == viewModel.value().timeline.clips.front().sourceNodeId &&
           !window.selectedAssetId().has_value() &&
           inspector.find("Inspector\nClip") != std::string::npos &&
           inspector.find("Type: video") != std::string::npos
      ? 0
      : 1;
  }

  if (emptyAddVideoUndoSmoke) {
    window.show();
    app.processEvents();
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    window.clickStewardPrimaryAction();
    const auto afterAdd = workspace.value().project().buildViewModel();
    if (!afterAdd) {
      printError(afterAdd.error());
      return 1;
    }
    window.undoLastEdit();
    const auto afterUndo = workspace.value().project().buildViewModel();
    if (!afterUndo) {
      printError(afterUndo.error());
      return 1;
    }
    std::cout << "afterAddRevision=" << afterAdd.value().project.revision.value() << '\n';
    std::cout << "afterAddAssets=" << afterAdd.value().assets.count << '\n';
    std::cout << "afterAddClips=" << afterAdd.value().timeline.clips.size() << '\n';
    std::cout << "afterAddCameras=" << afterAdd.value().timeline.cameras.size() << '\n';
    std::cout << "afterUndoRevision=" << afterUndo.value().project.revision.value() << '\n';
    std::cout << "afterUndoAssets=" << afterUndo.value().assets.count << '\n';
    std::cout << "afterUndoCompositions=" << afterUndo.value().timeline.compositions.size() << '\n';
    std::cout << "afterUndoLayers=" << afterUndo.value().timeline.layers.size() << '\n';
    std::cout << "afterUndoCameras=" << afterUndo.value().timeline.cameras.size() << '\n';
    std::cout << "afterUndoClips=" << afterUndo.value().timeline.clips.size() << '\n';
    return afterAdd.value().project.revision == grapple::foundation::RevisionId{"rev_2"} &&
           afterAdd.value().assets.count == 1 &&
           afterAdd.value().timeline.clips.size() == 1 &&
           afterAdd.value().timeline.cameras.size() == 1 &&
           afterUndo.value().project.revision == grapple::foundation::RevisionId{"rev_3"} &&
           afterUndo.value().assets.count == 1 &&
           afterUndo.value().timeline.compositions.empty() &&
           afterUndo.value().timeline.layers.empty() &&
           afterUndo.value().timeline.cameras.empty() &&
           afterUndo.value().timeline.clips.empty()
      ? 0
      : 1;
  }

  if (emptyAddTrackSmoke) {
    window.addTrack();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "compositions=" << viewModel.value().timeline.compositions.size() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_2"} &&
           viewModel.value().timeline.compositions.size() == 1 &&
           viewModel.value().timeline.layers.size() == 1 &&
           viewModel.value().timeline.clips.empty()
      ? 0
      : 1;
  }

  if (emptyAddCameraSmoke) {
    window.show();
    app.processEvents();
    const std::string stewardBefore = window.stewardContents();
    const std::string stewardActionBefore = window.stewardPrimaryActionText();
    const bool stewardActionEnabledBefore = window.stewardPrimaryActionEnabled();
    window.addCamera();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string stewardAfter = window.stewardContents();
    std::cout << "stewardBefore=" << stewardBefore << '\n';
    std::cout << "stewardActionBefore=" << stewardActionBefore << '\n';
    std::cout << "stewardActionEnabledBefore=" << (stewardActionEnabledBefore ? "true" : "false") << '\n';
    std::cout << "stewardAfter=" << stewardAfter << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "compositions=" << viewModel.value().timeline.compositions.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    if (window.selectedNodeId().has_value()) {
      std::cout << "selectedNode=" << window.selectedNodeId()->value() << '\n';
    }
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_2"} &&
           viewModel.value().timeline.compositions.size() == 1 &&
           viewModel.value().timeline.cameras.size() == 1 &&
           stewardActionBefore == "Start Sample" &&
           stewardActionEnabledBefore &&
           stewardBefore.find("0 assets | 0 clips | 0 cameras | 0 editable effects") != std::string::npos &&
           stewardAfter.find("0 assets | 0 clips | 1 cameras | 0 editable effects") != std::string::npos &&
           window.selectedNodeId().has_value() &&
           window.selectedNodeId().value() == viewModel.value().timeline.cameras.front().sourceNodeId
      ? 0
      : 1;
  }

  if (updateCameraSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setSelectedCameraNameControlValue("Renamed Camera");
    window.setSelectedCameraFocalLengthControlValue(85.0);
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.cameras.empty()) {
      std::cerr << "No cameras.\n";
      return 1;
    }
    const std::string inspector = window.inspectorContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "cameraName=" << viewModel.value().timeline.cameras.front().name << '\n';
    std::cout << "focalLength=" << viewModel.value().timeline.cameras.front().state.lens.focalLength << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           viewModel.value().timeline.cameras.front().name == "Renamed Camera" &&
           viewModel.value().timeline.cameras.front().state.lens.focalLength == 85.0 &&
           inspector.find("Inspector\nCamera\nName: Renamed Camera") != std::string::npos &&
           inspector.find("Focal Length: 85.0") != std::string::npos
      ? 0
      : 1;
  }

  if (stewardUpdateCameraSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Rename camera to \"Closeup\" and set camera focal length to 50.");
    const std::string primaryActionText = window.stewardPrimaryActionText();
    const bool primaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.cameras.empty()) {
      std::cerr << "No cameras.\n";
      return 1;
    }
    const auto conversation = workspace.value().steward().conversationState();
    const std::string inspector = window.inspectorContents();
    const std::string steward = window.stewardContents();
    const std::string log = window.logContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "cameraName=" << viewModel.value().timeline.cameras.front().name << '\n';
    std::cout << "focalLength=" << viewModel.value().timeline.cameras.front().state.lens.focalLength << '\n';
    std::cout << "primaryActionText=" << primaryActionText << '\n';
    std::cout << "primaryActionEnabled=" << (primaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           viewModel.value().timeline.cameras.front().name == "Closeup" &&
           viewModel.value().timeline.cameras.front().state.lens.focalLength == 50.0 &&
           window.selectedNodeId().has_value() &&
           window.selectedNodeId().value() == viewModel.value().timeline.cameras.front().sourceNodeId &&
           window.stewardIntent().empty() &&
           primaryActionText == "Update Camera" &&
           primaryActionEnabled &&
           conversation.runs.size() == 1 &&
           conversation.runs[0].toolCalls.size() == 1 &&
           conversation.runs[0].toolCalls[0].toolSerializedId == "camera.update" &&
           inspector.find("Inspector\nCamera\nName: Closeup") != std::string::npos &&
           inspector.find("Focal Length: 50.0") != std::string::npos &&
           steward.find("Camera on Closeup") != std::string::npos &&
           steward.find("Update Camera -> succeeded") != std::string::npos &&
           log.find("Steward updated selected camera") != std::string::npos
      ? 0
      : 1;
  }

  if (addNoteSmoke) {
    window.addNote();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string inspector = window.inspectorContents();
    const bool selectedClipMenuActionsEnabled = window.selectedClipMenuActionsEnabled();
    const bool selectedTrackMenuActionEnabled = window.selectedTrackMenuActionEnabled();
    const bool selectedNoteMenuActionEnabled = window.selectedNoteMenuActionEnabled();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "notes=" << viewModel.value().notes.rows.size() << '\n';
    std::cout << "selectedClipMenuActionsEnabled=" << (selectedClipMenuActionsEnabled ? "true" : "false") << '\n';
    std::cout << "selectedTrackMenuActionEnabled=" << (selectedTrackMenuActionEnabled ? "true" : "false") << '\n';
    std::cout << "selectedNoteMenuActionEnabled=" << (selectedNoteMenuActionEnabled ? "true" : "false") << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           viewModel.value().notes.rows.size() == 1 &&
           viewModel.value().notes.rows.front().title == "Note 1" &&
           !selectedClipMenuActionsEnabled &&
           !selectedTrackMenuActionEnabled &&
           selectedNoteMenuActionEnabled &&
           inspector.find("Inspector\nNote\nNote 1") != std::string::npos
      ? 0
      : 1;
  }

  if (updateNoteSmoke) {
    window.addNote();
    window.updateSelectedNote("Updated Note", "Updated project note");
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string inspector = window.inspectorContents();
    const bool selectedNoteMenuActionEnabled = window.selectedNoteMenuActionEnabled();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "notes=" << viewModel.value().notes.rows.size() << '\n';
    std::cout << "selectedNoteMenuActionEnabled=" << (selectedNoteMenuActionEnabled ? "true" : "false") << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           viewModel.value().notes.rows.size() == 1 &&
           viewModel.value().notes.rows.front().title == "Updated Note" &&
           viewModel.value().notes.rows.front().markdown == "Updated project note" &&
           selectedNoteMenuActionEnabled &&
           inspector.find("Inspector\nNote\nUpdated Note") != std::string::npos &&
           inspector.find("Updated project note") != std::string::npos
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

  if (trimClipSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    window.trimSelectedClipEnd(grapple::foundation::TimeSeconds{-1.0});
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.clips.empty()) {
      std::cerr << "No clips after trim.\n";
      return 1;
    }
    const grapple::app::AppClipRow& clip = viewModel.value().timeline.clips.front();
    std::cout << "timelineStart=" << clip.timelineRange.start.value << '\n';
    std::cout << "timelineEnd=" << clip.timelineRange.end.value << '\n';
    std::cout << "sourceStart=" << clip.sourceRange.start.value << '\n';
    std::cout << "sourceEnd=" << clip.sourceRange.end.value << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
    return clip.timelineRange.start == grapple::foundation::TimeSeconds{0.0} &&
           clip.timelineRange.end == grapple::foundation::TimeSeconds{9.0} &&
           clip.sourceRange.start == grapple::foundation::TimeSeconds{0.0} &&
           clip.sourceRange.end == grapple::foundation::TimeSeconds{9.0} &&
           viewModel.value().timeline.duration == grapple::foundation::TimeSeconds{9.0}
      ? 0
      : 1;
  }

  if (clipTimingPanelSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    window.setSelectedClipPropertyControlValue("clipTimelineStart", 1.0);
    window.setSelectedClipPropertyControlValue("clipTimelineEnd", 8.0);
    window.setSelectedClipPropertyControlValue("clipSourceStart", 0.5);
    window.setSelectedClipPropertyControlValue("clipSourceEnd", 7.5);
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.clips.empty()) {
      std::cerr << "No clips after clip timing panel edit.\n";
      return 1;
    }
    const grapple::app::AppClipRow& clip = viewModel.value().timeline.clips.front();
    const std::string inspector = window.inspectorContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "timelineStart=" << clip.timelineRange.start.value << '\n';
    std::cout << "timelineEnd=" << clip.timelineRange.end.value << '\n';
    std::cout << "sourceStart=" << clip.sourceRange.start.value << '\n';
    std::cout << "sourceEnd=" << clip.sourceRange.end.value << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_9"} &&
           clip.timelineRange.start == grapple::foundation::TimeSeconds{1.0} &&
           clip.timelineRange.end == grapple::foundation::TimeSeconds{8.0} &&
           clip.sourceRange.start == grapple::foundation::TimeSeconds{0.5} &&
           clip.sourceRange.end == grapple::foundation::TimeSeconds{7.5} &&
           viewModel.value().timeline.duration == grapple::foundation::TimeSeconds{8.0} &&
           inspector.find("Range: 1s - 8s") != std::string::npos
      ? 0
      : 1;
  }

  if (nudgeClipSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    window.setSelectedClipPropertyControlValue("clipTransformPositionX", 0.25);
    window.setSelectedClipPropertyControlValue("clipTransformPositionY", 0.5);
    window.setSelectedClipPropertyControlValue("clipTransformScaleX", 1.25);
    window.setSelectedClipPropertyControlValue("clipTransformScaleY", 1.25);
    window.setSelectedClipPropertyControlValue("clipTransformOpacity", 0.5);
    window.setSelectedClipPropertyControlValue("clipPlaybackRate", 1.5);
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.clips.empty()) {
      std::cerr << "No clips after nudge.\n";
      return 1;
    }
    const grapple::app::AppClipRow& clip = viewModel.value().timeline.clips.front();
    const std::string inspector = window.inspectorContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "positionX=" << clip.transform.position.x << '\n';
    std::cout << "positionY=" << clip.transform.position.y << '\n';
    std::cout << "scaleX=" << clip.transform.scale.x << '\n';
    std::cout << "scaleY=" << clip.transform.scale.y << '\n';
    std::cout << "opacity=" << clip.transform.opacity << '\n';
    std::cout << "playbackRate=" << clip.playbackRate << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_11"} &&
           clip.transform.position.x == 0.25 &&
           clip.transform.position.y == 0.5 &&
           clip.transform.scale.x == 1.25 &&
           clip.transform.scale.y == 1.25 &&
           clip.transform.opacity == 0.5 &&
           clip.playbackRate == 1.5 &&
           clip.timelineRange.start == grapple::foundation::TimeSeconds{0.0} &&
           clip.timelineRange.end == grapple::foundation::TimeSeconds{10.0} &&
           clip.sourceRange.start == grapple::foundation::TimeSeconds{0.0} &&
           clip.sourceRange.end == grapple::foundation::TimeSeconds{10.0} &&
           inspector.find("Position: 0.25, 0.50") != std::string::npos &&
           inspector.find("Scale: 1.25, 1.25") != std::string::npos &&
           inspector.find("Speed: 1.50x") != std::string::npos &&
           inspector.find("Opacity: 0.50") != std::string::npos
      ? 0
      : 1;
  }

  if (undoRedoSmoke) {
    const auto approx = [](double lhs, double rhs) {
      return std::abs(lhs - rhs) < 0.000001;
    };
    const auto renderCameraPositionX = [&]() -> grapple::foundation::Result<double> {
      const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
      const auto frame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
        previewState.playhead,
        grapple::render::RenderQuality::Draft
      });
      if (!frame) {
        return frame.error();
      }
      if (frame.value().frame.cameras.empty()) {
        return grapple::foundation::Error{"desktop.undo_redo_camera_missing", "Undo/redo smoke requires an evaluated camera."};
      }
      return frame.value().frame.cameras.front().state.transform.position.x;
    };
    const auto effectPositionParam = [](
      const grapple::app::AppViewModel& viewModel
    ) -> grapple::foundation::Result<const grapple::app::AppEffectParamRow*> {
      if (viewModel.timeline.effectGraphs.empty() || viewModel.timeline.effectGraphs.front().effects.empty()) {
        return grapple::foundation::Error{"desktop.undo_redo_effect_missing", "Undo/redo smoke requires an editable effect."};
      }
      const auto& params = viewModel.timeline.effectGraphs.front().effects.front().params;
      const auto param = std::find_if(params.begin(), params.end(), [](const grapple::app::AppEffectParamRow& row) {
        return row.name == grapple::effects::builtin_effect::PositionXParam;
      });
      if (param == params.end()) {
        return grapple::foundation::Error{"desktop.undo_redo_param_missing", "Undo/redo smoke requires position_x."};
      }
      return &*param;
    };

    window.addTrack();
    auto afterAdd = workspace.value().project().buildViewModel();
    if (!afterAdd) {
      printError(afterAdd.error());
      return 1;
    }
    const bool afterAddUndoActionEnabled = window.undoActionEnabled();
    const bool afterAddRedoActionEnabled = window.redoActionEnabled();
    window.undoLastEdit();
    auto afterUndo = workspace.value().project().buildViewModel();
    if (!afterUndo) {
      printError(afterUndo.error());
      return 1;
    }
    const bool afterUndoUndoActionEnabled = window.undoActionEnabled();
    const bool afterUndoRedoActionEnabled = window.redoActionEnabled();
    window.redoLastEdit();
    auto afterRedo = workspace.value().project().buildViewModel();
    if (!afterRedo) {
      printError(afterRedo.error());
      return 1;
    }
    const bool afterRedoUndoActionEnabled = window.undoActionEnabled();
    const bool afterRedoRedoActionEnabled = window.redoActionEnabled();
    std::cout << "afterAddRevision=" << afterAdd.value().project.revision.value() << '\n';
    std::cout << "afterAddLayers=" << afterAdd.value().timeline.layers.size() << '\n';
    std::cout << "afterAddUndoActionEnabled=" << (afterAddUndoActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterAddRedoActionEnabled=" << (afterAddRedoActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterUndoRevision=" << afterUndo.value().project.revision.value() << '\n';
    std::cout << "afterUndoLayers=" << afterUndo.value().timeline.layers.size() << '\n';
    std::cout << "afterUndoUndoActionEnabled=" << (afterUndoUndoActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterUndoRedoActionEnabled=" << (afterUndoRedoActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterRedoRevision=" << afterRedo.value().project.revision.value() << '\n';
    std::cout << "afterRedoLayers=" << afterRedo.value().timeline.layers.size() << '\n';
    std::cout << "afterRedoUndoActionEnabled=" << (afterRedoUndoActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterRedoRedoActionEnabled=" << (afterRedoRedoActionEnabled ? "true" : "false") << '\n';
    const bool trackUndoRedoOk =
      afterAdd.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
      afterAdd.value().timeline.layers.size() == 2 &&
      afterAddUndoActionEnabled &&
      !afterAddRedoActionEnabled &&
      afterUndo.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
      afterUndo.value().timeline.layers.size() == 1 &&
      afterUndoUndoActionEnabled &&
      afterUndoRedoActionEnabled &&
      afterRedo.value().project.revision == grapple::foundation::RevisionId{"rev_8"} &&
      afterRedo.value().timeline.layers.size() == 2 &&
      afterRedoUndoActionEnabled &&
      !afterRedoRedoActionEnabled;

    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Center the camera with editable controls.");
    window.clickStewardPrimaryAction();
    window.setEffectParamControlValue(grapple::effects::builtin_effect::PositionXParam, 0.25);

    const auto afterParamEdit = workspace.value().project().buildViewModel();
    if (!afterParamEdit) {
      printError(afterParamEdit.error());
      return 1;
    }
    auto editedParam = effectPositionParam(afterParamEdit.value());
    if (!editedParam) {
      printError(editedParam.error());
      return 1;
    }
    const auto editedCameraX = renderCameraPositionX();
    if (!editedCameraX) {
      printError(editedCameraX.error());
      return 1;
    }

    window.undoLastEdit();
    const auto afterParamUndo = workspace.value().project().buildViewModel();
    if (!afterParamUndo) {
      printError(afterParamUndo.error());
      return 1;
    }
    auto undoneParam = effectPositionParam(afterParamUndo.value());
    if (!undoneParam) {
      printError(undoneParam.error());
      return 1;
    }
    const auto undoneCameraX = renderCameraPositionX();
    if (!undoneCameraX) {
      printError(undoneCameraX.error());
      return 1;
    }
    window.redoLastEdit();
    const auto afterParamRedo = workspace.value().project().buildViewModel();
    if (!afterParamRedo) {
      printError(afterParamRedo.error());
      return 1;
    }
    auto redoneParam = effectPositionParam(afterParamRedo.value());
    if (!redoneParam) {
      printError(redoneParam.error());
      return 1;
    }
    const auto redoneCameraX = renderCameraPositionX();
    if (!redoneCameraX) {
      printError(redoneCameraX.error());
      return 1;
    }
    std::cout << "afterParamEditRevision=" << afterParamEdit.value().project.revision.value() << '\n';
    std::cout << "afterParamEditValue=" << std::get<double>(editedParam.value()->value) << '\n';
    std::cout << "afterParamEditCameraX=" << editedCameraX.value() << '\n';
    std::cout << "afterParamUndoRevision=" << afterParamUndo.value().project.revision.value() << '\n';
    std::cout << "afterParamUndoValue=" << std::get<double>(undoneParam.value()->value) << '\n';
    std::cout << "afterParamUndoCameraX=" << undoneCameraX.value() << '\n';
    std::cout << "afterParamRedoRevision=" << afterParamRedo.value().project.revision.value() << '\n';
    std::cout << "afterParamRedoValue=" << std::get<double>(redoneParam.value()->value) << '\n';
    std::cout << "afterParamRedoCameraX=" << redoneCameraX.value() << '\n';
    return trackUndoRedoOk &&
           std::holds_alternative<double>(editedParam.value()->value) &&
           std::holds_alternative<double>(undoneParam.value()->value) &&
           std::holds_alternative<double>(redoneParam.value()->value) &&
           approx(std::get<double>(editedParam.value()->value), 0.25) &&
           approx(editedCameraX.value(), 0.25) &&
           approx(std::get<double>(undoneParam.value()->value), 0.0) &&
           approx(undoneCameraX.value(), 0.0) &&
           !undoneParam.value()->lastEditedRevision.has_value() &&
           approx(std::get<double>(redoneParam.value()->value), 0.25) &&
           approx(redoneCameraX.value(), 0.25) &&
           redoneParam.value()->lastEditedRevision.has_value()
      ? 0
      : 1;
  }

  if (stewardSuggestionSmoke) {
    window.show();
    app.processEvents();
    window.startStarterSample();
    const int suggestions = window.stewardSuggestedRequestCount();
    const std::string firstSuggestion = window.stewardSuggestedRequestText(0);
    window.clickStewardSuggestedRequest(0);
    const std::string intentAfterSuggestion = window.stewardIntent();
    const std::string primaryActionText = window.stewardPrimaryActionText();
    const bool primaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.applyStewardSuggestedRequest(0);
    const std::string firstSuggestionAfterEffect = window.stewardSuggestedRequestText(0);
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string effectParamTitle = window.effectParamTitleText();
    const std::string inspector = window.inspectorContents();
    const std::string steward = window.stewardContents();
    std::cout << "suggestions=" << suggestions << '\n';
    std::cout << "firstSuggestion=" << firstSuggestion << '\n';
    std::cout << "intentAfterSuggestion=" << intentAfterSuggestion << '\n';
    std::cout << "primaryAction=" << primaryActionText << '\n';
    std::cout << "primaryActionEnabled=" << (primaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "firstSuggestionAfterEffect=" << firstSuggestionAfterEffect << '\n';
    std::cout << "effects=" << viewModel.value().timeline.effectCount << '\n';
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "steward=" << steward << '\n';
    return suggestions == 4 &&
           firstSuggestion == "Tint selected clip red." &&
           intentAfterSuggestion == firstSuggestion &&
           primaryActionText == "Apply Request To Clip" &&
           primaryActionEnabled &&
           firstSuggestionAfterEffect == "Make clip tint stronger and blue." &&
           viewModel.value().timeline.effectCount == 1 &&
           effectParamTitle == "Clip Tint on starter-gradient" &&
           inspector.find("Created by steward") != std::string::npos &&
           steward.find("Latest request: Tint selected clip red.") != std::string::npos
      ? 0
      : 1;
  }

  if (setEffectParamSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Create editable camera controls.");
    window.clickStewardPrimaryAction();
    window.setEffectParamControlDraftValue(grapple::effects::builtin_effect::ZoomParam, 1.5);
    const auto afterDraftEdit = workspace.value().project().buildViewModel();
    if (!afterDraftEdit) {
      printError(afterDraftEdit.error());
      return 1;
    }
    if (afterDraftEdit.value().timeline.effectGraphs.empty() ||
        afterDraftEdit.value().timeline.effectGraphs.front().effects.empty()) {
      std::cerr << "Set effect param smoke requires an editable effect.\n";
      return 1;
    }
    const auto draftZoomParam = std::find_if(
      afterDraftEdit.value().timeline.effectGraphs.front().effects.front().params.begin(),
      afterDraftEdit.value().timeline.effectGraphs.front().effects.front().params.end(),
      [](const grapple::app::AppEffectParamRow& row) {
        return row.name == grapple::effects::builtin_effect::ZoomParam;
      }
    );
    if (draftZoomParam == afterDraftEdit.value().timeline.effectGraphs.front().effects.front().params.end() ||
        !std::holds_alternative<double>(draftZoomParam->value)) {
      std::cerr << "Set effect param smoke requires a numeric zoom parameter.\n";
      return 1;
    }
    const auto draftPreviewFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!draftPreviewFrame) {
      printError(draftPreviewFrame.error());
      return 1;
    }
    window.setEffectParamSliderRatio(grapple::effects::builtin_effect::PositionXParam, 0.625);
    window.setEffectParamControlValue(grapple::effects::builtin_effect::ZoomParam, 1.5);
    window.setEffectParamSliderRatio(grapple::effects::builtin_effect::PositionXParam, 0.625);
    const std::string inspector = window.inspectorContents();
    const std::string log = window.logContents();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto adjustedPreviewFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!adjustedPreviewFrame) {
      printError(adjustedPreviewFrame.error());
      return 1;
    }
    const bool previewUpdatedAfterParamCommit =
      draftPreviewFrame.value().frame.sourceRevision == afterDraftEdit.value().project.revision &&
      adjustedPreviewFrame.value().frame.sourceRevision == viewModel.value().project.revision &&
      adjustedPreviewFrame.value().frame.cameras.size() == 1 &&
      adjustedPreviewFrame.value().frame.cameras.front().state.transform.position.x == 0.25 &&
      adjustedPreviewFrame.value().frame.cameras.front().state.transform.scale.x == 1.5;
    std::cout << "afterDraftRevision=" << afterDraftEdit.value().project.revision.value() << '\n';
    std::cout << "afterDraftZoom=" << std::get<double>(draftZoomParam->value) << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "previewUpdatedAfterParamCommit=" << (previewUpdatedAfterParamCommit ? "true" : "false") << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "log=" << log << '\n';
    return afterDraftEdit.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           std::get<double>(draftZoomParam->value) == 1.0 &&
           viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_8"} &&
           previewUpdatedAfterParamCommit &&
           inspector.find("Position X (position_x)=0.25") != std::string::npos &&
           inspector.find("Zoom (zoom)=1.5") != std::string::npos &&
           log.find("Updated effect parameter position_x") != std::string::npos &&
           log.find("Updated effect parameter zoom") != std::string::npos
      ? 0
      : 1;
  }

  if (deleteEffectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Add then remove an editable camera transform.");
    window.clickStewardPrimaryAction();
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
           inspector.find("No effects attached.") != std::string::npos
      ? 0
      : 1;
  }

  if (effectKeyframeSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Animate camera position with editable controls.");
    window.clickStewardPrimaryAction();
    window.seekTo(grapple::foundation::TimeSeconds{2.0});
    window.setEffectParamControlValue(grapple::effects::builtin_effect::PositionXParam, 0.25);
    window.setEffectParamKeyframeAtPlayhead(grapple::effects::builtin_effect::PositionXParam);
    const auto afterKeyframe = workspace.value().project().buildViewModel();
    if (!afterKeyframe) {
      printError(afterKeyframe.error());
      return 1;
    }
    window.setEffectParamControlValue(grapple::effects::builtin_effect::PositionXParam, 0.5);
    window.setEffectParamKeyframeAtPlayhead(grapple::effects::builtin_effect::PositionXParam);
    const auto afterKeyframeUpdate = workspace.value().project().buildViewModel();
    if (!afterKeyframeUpdate) {
      printError(afterKeyframeUpdate.error());
      return 1;
    }
    window.setEffectParamKeyframeAtPlayhead(grapple::effects::builtin_effect::PositionXParam);
    const auto afterNoopKeyframeUpdate = workspace.value().project().buildViewModel();
    if (!afterNoopKeyframeUpdate) {
      printError(afterNoopKeyframeUpdate.error());
      return 1;
    }
    const std::string buttonAtKeyframe = window.effectParamKeyframeButtonText(grapple::effects::builtin_effect::PositionXParam);
    window.seekTo(grapple::foundation::TimeSeconds{0.0});
    const std::string buttonAwayFromKeyframe = window.effectParamKeyframeButtonText(grapple::effects::builtin_effect::PositionXParam);
    window.seekTo(grapple::foundation::TimeSeconds{2.0});
    const std::string buttonBackAtKeyframe = window.effectParamKeyframeButtonText(grapple::effects::builtin_effect::PositionXParam);
    const std::string effectParamPanelAfterUpdate = window.effectParamPanelText();
    window.deleteEffectParamKeyframeControl(grapple::effects::builtin_effect::PositionXParam, 0);
    const auto afterKeyframeDelete = workspace.value().project().buildViewModel();
    if (!afterKeyframeDelete) {
      printError(afterKeyframeDelete.error());
      return 1;
    }

    const auto keyframesAfterSet = afterKeyframe.value().timeline.effectGraphs[0].effects[0].params[0].keyframes;
    const auto keyframesAfterUpdate = afterKeyframeUpdate.value().timeline.effectGraphs[0].effects[0].params[0].keyframes;
    const auto keyframesAfterDelete = afterKeyframeDelete.value().timeline.effectGraphs[0].effects[0].params[0].keyframes;
    std::cout << "afterSetRevision=" << afterKeyframe.value().project.revision.value() << '\n';
    std::cout << "afterSetKeyframes=" << keyframesAfterSet.size() << '\n';
    std::cout << "afterUpdateRevision=" << afterKeyframeUpdate.value().project.revision.value() << '\n';
    std::cout << "afterUpdateKeyframes=" << keyframesAfterUpdate.size() << '\n';
    std::cout << "afterNoopUpdateRevision=" << afterNoopKeyframeUpdate.value().project.revision.value() << '\n';
    std::cout << "buttonAtKeyframe=" << buttonAtKeyframe << '\n';
    std::cout << "buttonAwayFromKeyframe=" << buttonAwayFromKeyframe << '\n';
    std::cout << "buttonBackAtKeyframe=" << buttonBackAtKeyframe << '\n';
    std::cout << "effectParamPanelAfterUpdate=" << effectParamPanelAfterUpdate << '\n';
    std::cout << "afterDeleteRevision=" << afterKeyframeDelete.value().project.revision.value() << '\n';
    std::cout << "afterDeleteKeyframes=" << keyframesAfterDelete.size() << '\n';
    return keyframesAfterSet.size() == 1 &&
           keyframesAfterSet[0].time == grapple::foundation::TimeSeconds{2.0} &&
           std::get<double>(keyframesAfterSet[0].value) == 0.25 &&
           keyframesAfterSet[0].lastEditedRevision == afterKeyframe.value().project.revision &&
           keyframesAfterSet[0].lastEditedActorName == "desktop" &&
           keyframesAfterUpdate.size() == 1 &&
           keyframesAfterUpdate[0].keyframeId == keyframesAfterSet[0].keyframeId &&
           std::get<double>(keyframesAfterUpdate[0].value) == 0.5 &&
           keyframesAfterUpdate[0].lastEditedRevision == afterKeyframeUpdate.value().project.revision &&
           keyframesAfterUpdate[0].lastEditedActorName == "desktop" &&
           afterNoopKeyframeUpdate.value().project.revision == afterKeyframeUpdate.value().project.revision &&
           buttonAtKeyframe == "Update Key" &&
           buttonAwayFromKeyframe == "Add Key" &&
           buttonBackAtKeyframe == "Update Key" &&
           effectParamPanelAfterUpdate.find("2s = 0.5 last changed by desktop at ") != std::string::npos &&
           keyframesAfterDelete.empty()
      ? 0
      : 1;
  }

  if (stewardMotionSmoke) {
    const auto approx = [](double lhs, double rhs) {
      return std::abs(lhs - rhs) < 0.000001;
    };

    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Pan right with editable camera controls.");
    window.clickStewardPrimaryAction();
    window.seekTo(grapple::foundation::TimeSeconds{5.0});

    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.effectGraphs.empty() ||
        viewModel.value().timeline.effectGraphs.front().effects.empty()) {
      std::cerr << "Steward motion smoke requires an editable effect.\n";
      return 1;
    }

    const auto& effect = viewModel.value().timeline.effectGraphs.front().effects.front();
    const auto param = std::find_if(
      effect.params.begin(),
      effect.params.end(),
      [](const grapple::app::AppEffectParamRow& row) {
        return row.name == grapple::effects::builtin_effect::PositionXParam;
      }
    );
    if (param == effect.params.end()) {
      std::cerr << "Steward motion smoke requires position_x.\n";
      return 1;
    }

    const auto frame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!frame) {
      printError(frame.error());
      return 1;
    }

    const auto conversation = workspace.value().steward().conversationState();
    const std::string effectPanel = window.effectParamPanelText();
    const std::string inspector = window.inspectorContents();
    if (frame.value().frame.cameras.empty()) {
      std::cerr << "Steward motion smoke requires an evaluated camera.\n";
      return 1;
    }
    const double cameraX = frame.value().frame.cameras.front().state.transform.position.x;

    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    if (!conversation.runs.empty()) {
      std::cout << "toolCalls=" << conversation.runs.front().toolCalls.size() << '\n';
    }
    std::cout << "positionXKeyframes=" << param->keyframes.size() << '\n';
    std::cout << "cameraXAtMidpoint=" << cameraX << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "effectPanel=" << effectPanel << '\n';

    const bool stewardCreatedMotion =
      viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_8"} &&
      conversation.runs.size() == 1 &&
      conversation.runs.front().toolCalls.size() == 3 &&
      conversation.runs.front().toolCalls[0].toolSerializedId == "effect.create_node" &&
      conversation.runs.front().toolCalls[1].toolSerializedId == "effect.create_param_keyframe" &&
      conversation.runs.front().toolCalls[2].toolSerializedId == "effect.create_param_keyframe" &&
      param->keyframes.size() == 2 &&
      param->keyframes[0].time == grapple::foundation::TimeSeconds{0.0} &&
      approx(std::get<double>(param->keyframes[0].value), 0.0) &&
      param->keyframes[0].lastEditedActorName == "steward" &&
      param->keyframes[1].time == grapple::foundation::TimeSeconds{10.0} &&
      approx(std::get<double>(param->keyframes[1].value), 0.25) &&
      param->keyframes[1].lastEditedActorName == "steward" &&
	      frame.value().frame.sourceRevision == viewModel.value().project.revision &&
	      frame.value().frame.cameras.size() == 1 &&
	      approx(cameraX, 0.125) &&
	      inspector.find("Position X (position_x)=0.125") != std::string::npos &&
	      effectPanel.find("0s = 0 last changed by steward at ") != std::string::npos &&
	      effectPanel.find("10s = 0.25 last changed by steward at ") != std::string::npos;

	    window.seekTo(grapple::foundation::TimeSeconds{0.0});
	    window.startPlayback();
	    for (int frameIndex = 0; frameIndex < 150; ++frameIndex) {
	      window.advancePlaybackFrame();
	    }
	    const std::string playbackInspector = window.inspectorContents();
	    const std::optional<double> playbackEffectControlValue =
	      window.effectParamControlValue(grapple::effects::builtin_effect::PositionXParam);
	    const bool editControlsUpdatedDuringPlayback =
	      playbackInspector.find("Position X (position_x)=0.125") != std::string::npos &&
	      playbackEffectControlValue.has_value() &&
	      approx(playbackEffectControlValue.value(), 0.125);
	    window.pausePlayback();
	    std::cout << "playbackInspector=" << playbackInspector << '\n';
	    if (playbackEffectControlValue.has_value()) {
	      std::cout << "playbackEffectControlValue=" << playbackEffectControlValue.value() << '\n';
	    }

	    window.seekTo(grapple::foundation::TimeSeconds{10.0});
    window.setEffectParamControlValue(grapple::effects::builtin_effect::PositionXParam, 0.5);
    window.setEffectParamKeyframeAtPlayhead(grapple::effects::builtin_effect::PositionXParam);
    window.seekTo(grapple::foundation::TimeSeconds{5.0});

    const auto adjustedViewModel = workspace.value().project().buildViewModel();
    if (!adjustedViewModel) {
      printError(adjustedViewModel.error());
      return 1;
    }
    const auto& adjustedEffect = adjustedViewModel.value().timeline.effectGraphs.front().effects.front();
    const auto adjustedParam = std::find_if(
      adjustedEffect.params.begin(),
      adjustedEffect.params.end(),
      [](const grapple::app::AppEffectParamRow& row) {
        return row.name == grapple::effects::builtin_effect::PositionXParam;
      }
    );
    if (adjustedParam == adjustedEffect.params.end()) {
      std::cerr << "Steward motion smoke requires adjusted position_x.\n";
      return 1;
    }
    const auto adjustedFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!adjustedFrame) {
      printError(adjustedFrame.error());
      return 1;
    }
    if (adjustedFrame.value().frame.cameras.empty()) {
      std::cerr << "Steward motion smoke requires an adjusted evaluated camera.\n";
      return 1;
    }
    const double adjustedCameraX = adjustedFrame.value().frame.cameras.front().state.transform.position.x;

    std::cout << "adjustedRevision=" << adjustedViewModel.value().project.revision.value() << '\n';
    std::cout << "adjustedCameraXAtMidpoint=" << adjustedCameraX << '\n';

    return stewardCreatedMotion &&
           adjustedViewModel.value().project.revision == grapple::foundation::RevisionId{"rev_9"} &&
           adjustedParam->keyframes.size() == 2 &&
	           adjustedParam->keyframes[1].keyframeId == param->keyframes[1].keyframeId &&
	           adjustedParam->keyframes[1].time == grapple::foundation::TimeSeconds{10.0} &&
	           approx(std::get<double>(adjustedParam->keyframes[1].value), 0.5) &&
	           adjustedParam->keyframes[1].lastEditedActorName == "desktop" &&
	           adjustedFrame.value().frame.sourceRevision == adjustedViewModel.value().project.revision &&
	           approx(adjustedCameraX, 0.25) &&
	           editControlsUpdatedDuringPlayback
	      ? 0
      : 1;
  }

  if (stewardZoomMotionSmoke) {
    const auto approx = [](double lhs, double rhs) {
      return std::abs(lhs - rhs) < 0.000001;
    };

    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Zoom in over time with editable camera controls.");
    window.clickStewardPrimaryAction();
    window.seekTo(grapple::foundation::TimeSeconds{5.0});

    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.effectGraphs.empty() ||
        viewModel.value().timeline.effectGraphs.front().effects.empty()) {
      std::cerr << "Steward zoom motion smoke requires an editable effect.\n";
      return 1;
    }

    const auto& effect = viewModel.value().timeline.effectGraphs.front().effects.front();
    const auto param = std::find_if(
      effect.params.begin(),
      effect.params.end(),
      [](const grapple::app::AppEffectParamRow& row) {
        return row.name == grapple::effects::builtin_effect::ZoomParam;
      }
    );
    if (param == effect.params.end()) {
      std::cerr << "Steward zoom motion smoke requires zoom.\n";
      return 1;
    }

    const auto frame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!frame) {
      printError(frame.error());
      return 1;
    }
    if (frame.value().frame.cameras.empty()) {
      std::cerr << "Steward zoom motion smoke requires an evaluated camera.\n";
      return 1;
    }
    const double scaleAtMidpoint = frame.value().frame.cameras.front().state.transform.scale.x;

    const auto conversation = workspace.value().steward().conversationState();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    if (!conversation.runs.empty()) {
      std::cout << "toolCalls=" << conversation.runs.front().toolCalls.size() << '\n';
    }
    std::cout << "zoomKeyframes=" << param->keyframes.size() << '\n';
    std::cout << "scaleAtMidpoint=" << scaleAtMidpoint << '\n';

    const bool stewardCreatedZoom =
      viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_8"} &&
      conversation.runs.size() == 1 &&
      conversation.runs.front().toolCalls.size() == 3 &&
      conversation.runs.front().toolCalls[0].toolSerializedId == "effect.create_node" &&
      conversation.runs.front().toolCalls[1].toolSerializedId == "effect.create_param_keyframe" &&
      conversation.runs.front().toolCalls[2].toolSerializedId == "effect.create_param_keyframe" &&
      param->keyframes.size() == 2 &&
      param->keyframes[0].time == grapple::foundation::TimeSeconds{0.0} &&
      approx(std::get<double>(param->keyframes[0].value), 1.0) &&
      param->keyframes[0].lastEditedActorName == "steward" &&
      param->keyframes[1].time == grapple::foundation::TimeSeconds{10.0} &&
      approx(std::get<double>(param->keyframes[1].value), 1.5) &&
      param->keyframes[1].lastEditedActorName == "steward" &&
      frame.value().frame.sourceRevision == viewModel.value().project.revision &&
      approx(scaleAtMidpoint, 1.25);

    window.seekTo(grapple::foundation::TimeSeconds{10.0});
    window.setEffectParamControlValue(grapple::effects::builtin_effect::ZoomParam, 2.0);
    window.setEffectParamKeyframeAtPlayhead(grapple::effects::builtin_effect::ZoomParam);
    window.seekTo(grapple::foundation::TimeSeconds{5.0});

    const auto adjustedViewModel = workspace.value().project().buildViewModel();
    if (!adjustedViewModel) {
      printError(adjustedViewModel.error());
      return 1;
    }
    const auto& adjustedEffect = adjustedViewModel.value().timeline.effectGraphs.front().effects.front();
    const auto adjustedParam = std::find_if(
      adjustedEffect.params.begin(),
      adjustedEffect.params.end(),
      [](const grapple::app::AppEffectParamRow& row) {
        return row.name == grapple::effects::builtin_effect::ZoomParam;
      }
    );
    if (adjustedParam == adjustedEffect.params.end()) {
      std::cerr << "Steward zoom motion smoke requires adjusted zoom.\n";
      return 1;
    }
    const auto adjustedFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!adjustedFrame) {
      printError(adjustedFrame.error());
      return 1;
    }
    if (adjustedFrame.value().frame.cameras.empty()) {
      std::cerr << "Steward zoom motion smoke requires an adjusted evaluated camera.\n";
      return 1;
    }
    const double adjustedScaleAtMidpoint = adjustedFrame.value().frame.cameras.front().state.transform.scale.x;

    std::cout << "adjustedRevision=" << adjustedViewModel.value().project.revision.value() << '\n';
    std::cout << "adjustedScaleAtMidpoint=" << adjustedScaleAtMidpoint << '\n';

    return stewardCreatedZoom &&
           adjustedViewModel.value().project.revision == grapple::foundation::RevisionId{"rev_9"} &&
           adjustedParam->keyframes.size() == 2 &&
           adjustedParam->keyframes[1].keyframeId == param->keyframes[1].keyframeId &&
           adjustedParam->keyframes[1].time == grapple::foundation::TimeSeconds{10.0} &&
           approx(std::get<double>(adjustedParam->keyframes[1].value), 2.0) &&
           adjustedParam->keyframes[1].lastEditedActorName == "desktop" &&
           adjustedFrame.value().frame.sourceRevision == adjustedViewModel.value().project.revision &&
           approx(adjustedScaleAtMidpoint, 1.5)
      ? 0
      : 1;
  }

  if (addEffectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    const auto selectedClipBeforeCreate = window.selectedNodeId();
    const std::string clipEffectParamPanelBeforeCreate = window.effectParamPanelText();
    window.setStewardIntent("Center the walking subject with exposed controls.");
    window.clickStewardPrimaryAction();
    const auto selectedAfterCreate = window.selectedNodeId();
    window.clickFirstTimelineClip();
    const auto selectedClipBeforeShowControls = window.selectedNodeId();
    window.clickStewardPrimaryAction();
    const auto selectedAfterShowControls = window.selectedNodeId();
    const int stewardRecentEdits = window.stewardRecentEditCount();
    window.clickStewardRecentEdit(0);
    app.processEvents();
    const int stewardSelectedRecentEdit = window.stewardCurrentRecentEditRow();
    const auto selectedAfterRecentEdit = window.selectedNodeId();
    const std::string inspector = window.inspectorContents();
    const std::string logText = window.logContents();
    const std::string steward = window.stewardContents();
    const std::string stewardIntent = window.stewardIntent();
    const std::string stewardIntentPlaceholder = window.stewardIntentPlaceholder();
    const std::string stewardActionText = window.stewardPrimaryActionText();
    const bool stewardActionEnabled = window.stewardPrimaryActionEnabled();
    const std::string effectParamTitle = window.effectParamTitleText();
    const std::string effectParamPanel = window.effectParamPanelText();
    const bool positionXVisible = window.effectParamControlVisible(grapple::effects::builtin_effect::PositionXParam);
    const bool positionYVisible = window.effectParamControlVisible(grapple::effects::builtin_effect::PositionYParam);
    const bool zoomVisible = window.effectParamControlVisible(grapple::effects::builtin_effect::ZoomParam);
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
    std::cout << "stewardIntent=" << stewardIntent << '\n';
    std::cout << "stewardIntentPlaceholder=" << stewardIntentPlaceholder << '\n';
    std::cout << "stewardAction=" << stewardActionText << '\n';
    std::cout << "stewardActionEnabled=" << (stewardActionEnabled ? "true" : "false") << '\n';
    std::cout << "recentEdits=" << stewardRecentEdits << '\n';
    std::cout << "selectedRecentEdit=" << stewardSelectedRecentEdit << '\n';
    if (selectedAfterRecentEdit.has_value()) {
      std::cout << "selectedAfterRecentEdit=" << selectedAfterRecentEdit->value() << '\n';
    }
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
    std::cout << "effectParamPanel=" << effectParamPanel << '\n';
    std::cout << "positionXVisible=" << (positionXVisible ? "true" : "false") << '\n';
    std::cout << "positionYVisible=" << (positionYVisible ? "true" : "false") << '\n';
    std::cout << "zoomVisible=" << (zoomVisible ? "true" : "false") << '\n';
    const auto& snapshots = workspace.value().project().packageState().snapshots.records();
    const bool intentRecorded = !snapshots.empty() &&
                                snapshots.back().label.has_value() &&
                                snapshots.back().label.value() == "Center the walking subject with exposed controls.";
    if (viewModel.value().timeline.cameras.empty()) {
      std::cerr << "No camera in add-effect smoke.\n";
      return 1;
    }
    if (viewModel.value().timeline.clips.empty()) {
      std::cerr << "No clip in add-effect smoke.\n";
      return 1;
    }
    const grapple::foundation::NodeId expectedCameraNodeId = viewModel.value().timeline.cameras.front().sourceNodeId;
    const grapple::foundation::NodeId expectedClipNodeId = viewModel.value().timeline.clips.front().sourceNodeId;
    std::optional<grapple::foundation::RevisionId> createdRevision;
    const bool cameraHasEffect = std::any_of(
      viewModel.value().timeline.effectGraphs.begin(),
      viewModel.value().timeline.effectGraphs.end(),
      [&](const grapple::app::AppEffectGraphRow& graph) {
        if (graph.targetNodeId != expectedCameraNodeId ||
            graph.effects.size() != 1 ||
            graph.effects.front().displayName != "Camera Transform" ||
            graph.effects.front().implementationKind != "builtin" ||
            !graph.effects.front().cameraTransformEffect ||
            !graph.effects.front().createdRevision.has_value()) {
          return false;
        }
        createdRevision = graph.effects.front().createdRevision;
        return true;
      }
    );
    const std::string createdRevisionText = createdRevision.has_value() ? createdRevision->value() : std::string{};
    return cameraHasEffect &&
           viewModel.value().timeline.effectCount == 1 &&
           inspector.find("Camera Transform") != std::string::npos &&
           inspector.find("Created by steward at " + createdRevisionText + ": Center the walking subject with exposed controls.") != std::string::npos &&
           inspector.find("[builtin]") == std::string::npos &&
           inspector.find("Entrypoint:") == std::string::npos &&
           inspector.find("Position X (position_x)=0") != std::string::npos &&
           intentRecorded &&
           stewardRecentEdits == 1 &&
           stewardSelectedRecentEdit == 0 &&
           steward.find("Applied edits: select one to inspect its target.") != std::string::npos &&
           steward.find("Latest result: Camera Transform on Camera (" + createdRevisionText + ")") != std::string::npos &&
           steward.find("Latest request: Center the walking subject with exposed controls.") != std::string::npos &&
           steward.find("Recent runs:") != std::string::npos &&
           steward.find("- Center the walking subject with exposed controls. [succeeded]") != std::string::npos &&
           steward.find("Create Effect Node -> succeeded at " + createdRevisionText) != std::string::npos &&
           steward.find("Create Effect Node -> failed") == std::string::npos &&
           steward.find("- Center the walking subject with exposed controls.") != std::string::npos &&
           selectedClipBeforeCreate.has_value() &&
           selectedClipBeforeCreate.value() == expectedClipNodeId &&
           clipEffectParamPanelBeforeCreate.find("editable clip controls") != std::string::npos &&
           clipEffectParamPanelBeforeCreate.find("exposure/brighten/darken") != std::string::npos &&
           clipEffectParamPanelBeforeCreate.find("camera controls") == std::string::npos &&
           selectedAfterCreate.has_value() &&
           selectedAfterCreate.value() == expectedCameraNodeId &&
           selectedClipBeforeShowControls.has_value() &&
           selectedClipBeforeShowControls.value() == expectedClipNodeId &&
           selectedAfterShowControls.has_value() &&
           selectedAfterShowControls.value() == expectedCameraNodeId &&
           selectedAfterRecentEdit.has_value() &&
           selectedAfterRecentEdit.value() == expectedCameraNodeId &&
           stewardIntent.empty() &&
           stewardIntentPlaceholder.find("set camera focal length") != std::string::npos &&
           stewardIntentPlaceholder.find("zoom in a little") != std::string::npos &&
           stewardActionText == "Type Request To Apply Camera Controls" &&
           !stewardActionEnabled &&
           effectParamTitle == "Camera Transform on Camera" &&
           effectParamPanel.find("Position X") != std::string::npos &&
           effectParamPanel.find("Position Y") != std::string::npos &&
           effectParamPanel.find("Zoom") != std::string::npos &&
           positionXVisible &&
           positionYVisible &&
           zoomVisible &&
           logText.find("Preview refreshed") == std::string::npos &&
           logText.find("agent.camera_transform_exists") == std::string::npos &&
           logText.find("runtime.effect_runtime_missing") == std::string::npos
      ? 0
      : 1;
  }

  if (stewardSubmitShortcutSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Center with editable camera controls.");
    window.pressStewardSubmitShortcut();
    app.processEvents();

    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string effectParamTitle = window.effectParamTitleText();
    const std::string stewardIntent = window.stewardIntent();
    const std::string steward = window.stewardContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "effects=" << viewModel.value().timeline.effectCount << '\n';
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
    std::cout << "stewardIntent=" << stewardIntent << '\n';
    std::cout << "steward=" << steward << '\n';
    return viewModel.value().timeline.effectCount == 1 &&
           viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           effectParamTitle == "Camera Transform on Camera" &&
           stewardIntent.empty() &&
           steward.find("- Center with editable camera controls. [succeeded]") != std::string::npos
      ? 0
      : 1;
  }

  if (clipEffectControlsSmoke) {
    const auto beforeEffect = workspace.value().project().buildViewModel();
    if (!beforeEffect) {
      printError(beforeEffect.error());
      return 1;
    }
    if (beforeEffect.value().timeline.clips.empty()) {
      std::cerr << "Clip effect controls smoke requires a visual clip.\n";
      return 1;
    }

    const grapple::foundation::NodeId clipNodeId = beforeEffect.value().timeline.clips.front().sourceNodeId;
    const std::string effectSource = "def prepare(ctx):\n  return {}\n";
    const auto effect = workspace.value().commandWriter().apply(
      grapple::project::CreateEffectCommand{
        workspace.value().commandWriter().nextNodeId("effect"),
        clipNodeId,
        workspace.value().commandWriter().nextEdgeId("effect targets clip"),
        grapple::timeline::EffectPayload{
          "Clip Follow",
          grapple::timeline::EffectImplementation{
            grapple::timeline::EffectImplementationKind::Python,
            "prepare",
            grapple::timeline::EffectSource{
              grapple::timeline::EffectSourceKind::InlineSource,
              "python",
              effectSource,
              std::nullopt,
              grapple::foundation::stableHash(effectSource)
            }
          },
          grapple::timeline::EffectPortSet{
            {grapple::timeline::EffectPort{"frame"}},
            {grapple::timeline::EffectPort{"clip_transform"}}
          },
          grapple::timeline::ParamSet{
            {grapple::timeline::Param{
              "target_x",
              0.5,
              grapple::timeline::Param::Control{
                "Target X",
                grapple::timeline::Param::NumericControl{0.0, 1.0, 0.01}
              }
            }}
          },
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            beforeEffect.value().timeline.duration
          }
        },
        grapple::graph::PortName{"clip_transform"},
        grapple::graph::PortName{"input"},
        0
      },
      desktopUserSource()
    );
    if (!effect) {
      printError(effect.error());
      return 1;
    }

    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    app.processEvents();

    const auto afterSelect = workspace.value().project().buildViewModel();
    if (!afterSelect) {
      printError(afterSelect.error());
      return 1;
    }
    const std::string selectedTab = window.currentDetailTabText();
    const std::string effectParamTitle = window.effectParamTitleText();
    const bool targetXVisible = window.effectParamControlVisible("target_x");
    std::cout << "revision=" << afterSelect.value().project.revision.value() << '\n';
    std::cout << "effectGraphs=" << afterSelect.value().timeline.effectGraphs.size() << '\n';
    std::cout << "selectedTab=" << selectedTab << '\n';
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
    std::cout << "targetXVisible=" << (targetXVisible ? "true" : "false") << '\n';
    return afterSelect.value().timeline.effectCount == 1 &&
           window.selectedNodeId().has_value() &&
           window.selectedNodeId().value() == clipNodeId &&
           selectedTab == "Effects" &&
           effectParamTitle.find("Clip Follow on ") == 0 &&
           targetXVisible
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

  if (deleteTrackSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineTrack();
    window.deleteSelectedTrack();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "tracks=" << viewModel.value().timeline.layers.size()
              << " clips=" << viewModel.value().timeline.clips.size() << '\n';
    return viewModel.value().timeline.layers.empty() && viewModel.value().timeline.clips.empty() ? 0 : 1;
  }

  if (stewardDeleteTrackSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineTrack();
    window.setStewardIntent("Delete selected track.");
    const std::string primaryActionText = window.stewardPrimaryActionText();
    const bool primaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto conversation = workspace.value().steward().conversationState();
    const std::string steward = window.stewardContents();
    const std::string log = window.logContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    std::cout << "primaryActionText=" << primaryActionText << '\n';
    std::cout << "primaryActionEnabled=" << (primaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           viewModel.value().timeline.layers.empty() &&
           viewModel.value().timeline.clips.empty() &&
           viewModel.value().timeline.cameras.size() == 1 &&
           !window.selectedNodeId().has_value() &&
           window.stewardIntent().empty() &&
           primaryActionText == "Apply Request To Track" &&
           primaryActionEnabled &&
           conversation.runs.size() == 1 &&
           conversation.runs[0].toolCalls.size() == 1 &&
           conversation.runs[0].toolCalls[0].toolSerializedId == "timeline.delete_track" &&
           steward.find("Track Delete") != std::string::npos &&
           steward.find("Delete selected track.") != std::string::npos &&
           steward.find("Delete Timeline Track -> succeeded") != std::string::npos &&
           log.find("Steward deleted selected track") != std::string::npos
      ? 0
      : 1;
  }

  if (stewardCreateTrackSmoke) {
    window.show();
    app.processEvents();
    window.setStewardIntent("Create audio layer.");
    const std::string primaryActionText = window.stewardPrimaryActionText();
    const bool primaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto conversation = workspace.value().steward().conversationState();
    const std::string steward = window.stewardContents();
    const std::string log = window.logContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    std::cout << "audioTracks=" << viewModel.value().timeline.audioTracks.size() << '\n';
    std::cout << "primaryActionText=" << primaryActionText << '\n';
    std::cout << "primaryActionEnabled=" << (primaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           viewModel.value().timeline.layers.size() == 1 &&
           viewModel.value().timeline.audioTracks.size() == 1 &&
           viewModel.value().timeline.audioTracks[0].name == "Audio Track" &&
           window.selectedNodeId().has_value() &&
           window.selectedNodeId().value() == viewModel.value().timeline.audioTracks[0].sourceNodeId &&
           window.stewardIntent().empty() &&
           primaryActionText == "Create Timeline Track" &&
           primaryActionEnabled &&
           conversation.runs.size() == 1 &&
           conversation.runs[0].toolCalls.size() == 1 &&
           conversation.runs[0].toolCalls[0].toolSerializedId == "timeline.create_track" &&
           steward.find("Track") != std::string::npos &&
           steward.find("Create audio layer.") != std::string::npos &&
           steward.find("Create Timeline Track -> succeeded") != std::string::npos &&
           log.find("Steward created timeline track") != std::string::npos
      ? 0
      : 1;
  }

  if (playbackSmoke) {
    const bool beforePlayActionEnabled = window.playActionEnabled();
    const bool beforePauseActionEnabled = window.pauseActionEnabled();
    const bool beforeSeekActionEnabled = window.seekActionEnabled();
    window.pressPlaybackShortcut();
    const bool duringPlayActionEnabled = window.playActionEnabled();
    const bool duringPauseActionEnabled = window.pauseActionEnabled();
    const bool duringSeekActionEnabled = window.seekActionEnabled();
    window.advancePlaybackFrame();
    window.pressPlaybackShortcut();
    const bool afterPausePlayActionEnabled = window.playActionEnabled();
    const bool afterPausePauseActionEnabled = window.pauseActionEnabled();
    const bool afterPauseSeekActionEnabled = window.seekActionEnabled();
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    const auto frame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      previewState.playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!frame) {
      printError(frame.error());
      return 1;
    }
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "beforePlayActionEnabled=" << (beforePlayActionEnabled ? "true" : "false") << '\n';
    std::cout << "beforePauseActionEnabled=" << (beforePauseActionEnabled ? "true" : "false") << '\n';
    std::cout << "beforeSeekActionEnabled=" << (beforeSeekActionEnabled ? "true" : "false") << '\n';
    std::cout << "duringPlayActionEnabled=" << (duringPlayActionEnabled ? "true" : "false") << '\n';
    std::cout << "duringPauseActionEnabled=" << (duringPauseActionEnabled ? "true" : "false") << '\n';
    std::cout << "duringSeekActionEnabled=" << (duringSeekActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterPausePlayActionEnabled=" << (afterPausePlayActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterPausePauseActionEnabled=" << (afterPausePauseActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterPauseSeekActionEnabled=" << (afterPauseSeekActionEnabled ? "true" : "false") << '\n';
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    std::cout << "frameTime=" << frame.value().frame.time.value << '\n';
    std::cout << "frameRevision=" << frame.value().frame.sourceRevision.value() << '\n';
    return beforePlayActionEnabled &&
           !beforePauseActionEnabled &&
           beforeSeekActionEnabled &&
           !duringPlayActionEnabled &&
           duringPauseActionEnabled &&
           duringSeekActionEnabled &&
           afterPausePlayActionEnabled &&
           !afterPausePauseActionEnabled &&
           afterPauseSeekActionEnabled &&
           previewState.playhead.value > 0.0 &&
           frame.value().frame.time == previewState.playhead &&
           frame.value().frame.sourceRevision == viewModel.value().project.revision
      ? 0
      : 1;
  }

  if (openPackageSmoke) {
    const auto write = workspace.value().writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    const bool hadSelectedAssetBeforeOpen = window.selectedAssetId().has_value();
    window.openPackageRoot(grapple::foundation::FilePath{packageRoot.string()});
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "commands=" << workspace.value().project().packageState().commandLog.records().size() << '\n';
    std::cout << "hadSelectedAssetBeforeOpen=" << (hadSelectedAssetBeforeOpen ? "true" : "false") << '\n';
    std::cout << "hasSelectedAssetAfterOpen=" << (window.selectedAssetId().has_value() ? "true" : "false") << '\n';
    return viewModel.value().project.projectId == grapple::foundation::ProjectId{"proj_desktop"} &&
           viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_5"} &&
           workspace.value().project().packageState().commandLog.records().size() == 5 &&
           hadSelectedAssetBeforeOpen &&
           !window.selectedAssetId().has_value()
      ? 0
      : 1;
  }

  if (newPackageSmoke) {
    const std::filesystem::path newPackageRoot = smokeRoot / "desktop-new-package";
    std::filesystem::remove_all(newPackageRoot);
    window.newPackageRoot(
      grapple::foundation::FilePath{newPackageRoot.string()},
      "Desktop New Package"
    );
    auto reopened = grapple::app::NativeWorkspaceSession::openPackageRoot(
      grapple::foundation::FilePath{newPackageRoot.string()}
    );
    if (!reopened) {
      printError(reopened.error());
      return 1;
    }
    const auto viewModel = reopened.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
    std::cout << "name=" << viewModel.value().project.name << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    return viewModel.value().project.projectId == grapple::foundation::ProjectId{"proj_desktop_new_package"} &&
           viewModel.value().project.name == "Desktop New Package" &&
           viewModel.value().assets.count == 0 &&
           viewModel.value().timeline.clips.empty() &&
           std::filesystem::exists(newPackageRoot / "manifest.json") &&
           std::filesystem::exists(newPackageRoot / "agent/runs.json") &&
           std::filesystem::exists(newPackageRoot / "agent/events.json")
      ? 0
      : 1;
  }

  if (exportSettingsSmoke) {
    window.show();
    app.processEvents();
    const std::filesystem::path outputPath = smokeRoot / "export-settings.avi";
    std::filesystem::remove(outputPath);
    window.setExportResolutionControlValue(320, 180);
    window.setExportFrameRateControlValue(10.0);
    window.setExportCodecControlValue("mjpeg");
    window.startPlayback();
    const bool playbackStartedBeforeExport = window.pauseActionEnabled();
    const bool exportStarted = window.startExportVideoFile(grapple::foundation::FilePath{outputPath.string()});
    const bool playActionEnabledDuringExport = window.playActionEnabled();
    const bool pauseActionEnabledDuringExport = window.pauseActionEnabled();
    const bool seekActionEnabledDuringExport = window.seekActionEnabled();
    const bool exportActionEnabledDuringExport = window.exportActionEnabled();
    const std::string detailTabDuringExport = window.currentDetailTabText();
    window.waitForExportIdle();
    const std::string detailTabAfterExport = window.currentDetailTabText();
    const bool playActionEnabledAfterExport = window.playActionEnabled();
    const bool pauseActionEnabledAfterExport = window.pauseActionEnabled();
    const bool seekActionEnabledAfterExport = window.seekActionEnabled();
    const std::string exportStatus = window.exportStatusText();
    const std::string log = window.logContents();
    const bool exists = std::filesystem::exists(outputPath);
    const auto size = exists ? std::filesystem::file_size(outputPath) : 0U;
    const auto encodedResolution = encodedVideoResolution(outputPath);
    if (!encodedResolution) {
      printError(encodedResolution.error());
      return 1;
    }
    std::cout << "playbackStartedBeforeExport=" << (playbackStartedBeforeExport ? "true" : "false") << '\n';
    std::cout << "exportStarted=" << (exportStarted ? "true" : "false") << '\n';
    std::cout << "playActionEnabledDuringExport=" << (playActionEnabledDuringExport ? "true" : "false") << '\n';
    std::cout << "pauseActionEnabledDuringExport=" << (pauseActionEnabledDuringExport ? "true" : "false") << '\n';
    std::cout << "seekActionEnabledDuringExport=" << (seekActionEnabledDuringExport ? "true" : "false") << '\n';
    std::cout << "exportActionEnabledDuringExport=" << (exportActionEnabledDuringExport ? "true" : "false") << '\n';
    std::cout << "detailTabDuringExport=" << detailTabDuringExport << '\n';
    std::cout << "detailTabAfterExport=" << detailTabAfterExport << '\n';
    std::cout << "playActionEnabledAfterExport=" << (playActionEnabledAfterExport ? "true" : "false") << '\n';
    std::cout << "pauseActionEnabledAfterExport=" << (pauseActionEnabledAfterExport ? "true" : "false") << '\n';
    std::cout << "seekActionEnabledAfterExport=" << (seekActionEnabledAfterExport ? "true" : "false") << '\n';
    std::cout << "exists=" << (exists ? "true" : "false") << '\n';
    std::cout << "size=" << size << '\n';
    std::cout << "encodedResolution=" << encodedResolution.value().width << "x" << encodedResolution.value().height << '\n';
    std::cout << "exportStatus=" << exportStatus << '\n';
    std::cout << "log=" << log << '\n';
    return playbackStartedBeforeExport &&
           exportStarted &&
           !playActionEnabledDuringExport &&
           !pauseActionEnabledDuringExport &&
           !seekActionEnabledDuringExport &&
           !exportActionEnabledDuringExport &&
           detailTabDuringExport == "Export" &&
           detailTabAfterExport == "Export" &&
           playActionEnabledAfterExport &&
           !pauseActionEnabledAfterExport &&
           seekActionEnabledAfterExport &&
           exists &&
           size > 0U &&
           encodedResolution.value() == grapple::foundation::Resolution{320, 180} &&
           exportStatus.find("Export complete: 100 frames, plan ") != std::string::npos &&
           exportStatus.find(outputPath.string()) != std::string::npos &&
           log.find("Export progress 100%") != std::string::npos &&
           log.find("Export evaluated 100 frames") != std::string::npos
      ? 0
      : 1;
  }

  if (productLoopSmoke) {
    window.show();
    app.processEvents();
    const std::filesystem::path outputPath = smokeRoot / "product-loop.avi";
    std::filesystem::remove(outputPath);

    window.startStarterSample();
    const std::string stewardActionAfterSampleStart = window.stewardPrimaryActionText();
    const bool stewardActionEnabledAfterSampleStart = window.stewardPrimaryActionEnabled();
    const bool addMediaActionEnabledAfterSampleStart = window.addSelectedMediaActionEnabled();
    const bool exportActionEnabledAfterMediaPlacement = window.exportActionEnabled();
    const auto basePreviewFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!basePreviewFrame) {
      printError(basePreviewFrame.error());
      return 1;
    }
    window.setStewardIntent("Tint selected clip red.");
    const std::string clipTintPrimaryActionText = window.stewardPrimaryActionText();
    const bool clipTintPrimaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Make clip tint stronger and blue.");
    const std::string clipTintUpdatePrimaryActionText = window.stewardPrimaryActionText();
    const bool clipTintUpdatePrimaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    window.setEffectParamVec3ControlValue(
      grapple::effects::builtin_effect::ClipTintColorParam,
      grapple::foundation::Vec3{0.2, 1.0, 0.35}
    );
    window.setStewardIntent("Add title \"Opening Title\".");
    const std::string textCreatePrimaryActionText = window.stewardPrimaryActionText();
    const bool textCreatePrimaryActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    window.setSelectedTextClipTextControlValue("MVP Opening");
    window.setSelectedTextClipPropertyControlValue("textClipFontSize", 72.0);
    window.setSelectedTextClipPropertyControlValue("textClipOpacity", 0.75);
    window.setStewardIntent("Center the subject with editable camera controls.");
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Move the camera framing right.");
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Make the subject bigger.");
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Recenter the subject.");
    window.clickStewardPrimaryAction();
    const int stewardRecentEdits = window.stewardRecentEditCount();
    window.clickStewardRecentEdit(0);
    const int stewardSelectedRecentEdit = window.stewardCurrentRecentEditRow();
    const std::string stewardSelectedRecentEditText = window.stewardRecentEditText(0);
    window.setEffectParamControlValue(grapple::effects::builtin_effect::ZoomParam, 1.6);
    const auto tunedViewModel = workspace.value().project().buildViewModel();
    if (!tunedViewModel) {
      printError(tunedViewModel.error());
      return 1;
    }
    const auto tunedPreviewFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!tunedPreviewFrame) {
      printError(tunedPreviewFrame.error());
      return 1;
    }
    const bool hasEvaluatedTunedPreview =
      tunedPreviewFrame.value().frame.sourceRevision == tunedViewModel.value().project.revision &&
      tunedPreviewFrame.value().frame.cameras.size() == 1 &&
      tunedPreviewFrame.value().frame.cameras.front().state.transform.position.x == 0.0 &&
      tunedPreviewFrame.value().frame.cameras.front().state.transform.scale.x == 1.6 &&
      tunedPreviewFrame.value().frame.mediaFrames.size() == 1 &&
      tunedPreviewFrame.value().frame.mediaFrames.front().tintColor.has_value() &&
      tunedPreviewFrame.value().frame.mediaFrames.front().tintColor.value() == grapple::foundation::Vec3{0.2, 1.0, 0.35} &&
      tunedPreviewFrame.value().frame.mediaFrames.front().tintAmount == 0.6 &&
      tunedPreviewFrame.value().frame.textFrames.size() == 1 &&
      tunedPreviewFrame.value().frame.textFrames.front().text == "MVP Opening" &&
      tunedPreviewFrame.value().frame.textFrames.front().style.fontSize == 72.0 &&
      tunedPreviewFrame.value().frame.textFrames.front().transform.opacity == 0.75;
    const bool previewPixelsChanged =
      basePreviewFrame.value().frame.image.has_value() &&
      tunedPreviewFrame.value().frame.image.has_value() &&
      basePreviewFrame.value().frame.image->resolution == tunedPreviewFrame.value().frame.image->resolution &&
      basePreviewFrame.value().frame.image->rgbaPixels != tunedPreviewFrame.value().frame.image->rgbaPixels;
    window.setExportResolutionControlValue(320, 180);
    window.setExportFrameRateControlValue(10.0);
    window.setExportCodecControlValue("mjpeg");
    window.exportVideoFile(grapple::foundation::FilePath{outputPath.string()});

    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string steward = window.stewardContents();
    const std::string stewardIntent = window.stewardIntent();
    const std::string inspector = window.inspectorContents();
    const auto selectedAfterRecentEdit = window.selectedNodeId();
    const std::string stewardActionText = window.stewardPrimaryActionText();
    const bool stewardActionEnabled = window.stewardPrimaryActionEnabled();
    const std::string effectParamTitle = window.effectParamTitleText();
    const std::string effectParamPanel = window.effectParamPanelText();
    const std::string exportStatus = window.exportStatusText();
    const std::string log = window.logContents();
    const bool exists = std::filesystem::exists(outputPath);
    const auto size = exists ? std::filesystem::file_size(outputPath) : 0U;
    const auto encodedResolution = encodedVideoResolution(outputPath);
    if (!encodedResolution) {
      printError(encodedResolution.error());
      return 1;
    }
    const auto productLoopWrite = workspace.value().writePackage();
    if (!productLoopWrite) {
      printError(productLoopWrite.error());
      return 1;
    }
    auto reopenedProductLoopWorkspace = grapple::app::NativeWorkspaceSession::openPackageRoot(
      workspace.value().project().packageState().package.rootPath
    );
    if (!reopenedProductLoopWorkspace) {
      printError(reopenedProductLoopWorkspace.error());
      return 1;
    }
    const auto reopenedViewModel = reopenedProductLoopWorkspace.value().project().buildViewModel();
    if (!reopenedViewModel) {
      printError(reopenedViewModel.error());
      return 1;
    }
    const auto reopenedPreviewRefresh = reopenedProductLoopWorkspace.value().preview().refreshFromProject();
    if (!reopenedPreviewRefresh) {
      printError(reopenedPreviewRefresh.error());
      return 1;
    }
    const auto reopenedPreviewFrame = reopenedProductLoopWorkspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      reopenedProductLoopWorkspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!reopenedPreviewFrame) {
      printError(reopenedPreviewFrame.error());
      return 1;
    }
    const auto reopenedConversation = reopenedProductLoopWorkspace.value().steward().conversationState();
    const bool reopenedProductLoopMatches =
      reopenedViewModel.value().project.revision == viewModel.value().project.revision &&
      reopenedViewModel.value().assets.count == 1 &&
      reopenedViewModel.value().timeline.clips.size() == 1 &&
      reopenedViewModel.value().timeline.textClips.size() == 1 &&
      reopenedViewModel.value().timeline.textClips.front().text == "MVP Opening" &&
      reopenedViewModel.value().timeline.textClips.front().style.fontSize == 72.0 &&
      reopenedViewModel.value().timeline.textClips.front().transform.opacity == 0.75 &&
      reopenedViewModel.value().timeline.cameras.size() == 1 &&
      reopenedViewModel.value().timeline.effectCount == 2 &&
      reopenedConversation.diagnostics.empty() &&
      reopenedConversation.runs.size() == 8 &&
      reopenedConversation.runs.front().toolCalls.size() == 1 &&
      reopenedConversation.runs.front().toolCalls.front().toolSerializedId == "timeline.place_asset" &&
      reopenedConversation.runs.back().toolCalls.size() == 1 &&
      reopenedConversation.runs.back().toolCalls.front().toolSerializedId == "effect.update_param_value" &&
      reopenedPreviewFrame.value().frame.sourceRevision == reopenedViewModel.value().project.revision &&
      reopenedPreviewFrame.value().frame.mediaFrames.size() == 1 &&
      reopenedPreviewFrame.value().frame.mediaFrames.front().tintColor.has_value() &&
      reopenedPreviewFrame.value().frame.mediaFrames.front().tintColor.value() == grapple::foundation::Vec3{0.2, 1.0, 0.35} &&
      reopenedPreviewFrame.value().frame.mediaFrames.front().tintAmount == 0.6 &&
      reopenedPreviewFrame.value().frame.textFrames.size() == 1 &&
      reopenedPreviewFrame.value().frame.textFrames.front().text == "MVP Opening" &&
      reopenedPreviewFrame.value().frame.textFrames.front().style.fontSize == 72.0 &&
      reopenedPreviewFrame.value().frame.textFrames.front().transform.opacity == 0.75 &&
      reopenedPreviewFrame.value().frame.cameras.size() == 1 &&
      reopenedPreviewFrame.value().frame.cameras.front().state.transform.position.x == 0.0 &&
      reopenedPreviewFrame.value().frame.cameras.front().state.transform.scale.x == 1.6;
    const std::string expectedExportProvenance =
      "Export evaluated 100 frames from " +
      viewModel.value().project.revision.value() +
      " plan " +
      tunedPreviewFrame.value().frame.renderPlanHash.toHex().substr(0, 8);
    const bool hasTunedEditableEffect = std::any_of(
      viewModel.value().timeline.effectGraphs.begin(),
      viewModel.value().timeline.effectGraphs.end(),
      [](const grapple::app::AppEffectGraphRow& graph) {
        return std::any_of(
          graph.effects.begin(),
          graph.effects.end(),
          [](const grapple::app::AppEffectRow& effect) {
            if (!effect.cameraTransformEffect) {
              return false;
            }
            const auto positionParam = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::PositionXParam;
              }
            );
            const auto zoomParam = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::ZoomParam;
              }
            );
            return positionParam != effect.params.end() &&
                   std::holds_alternative<double>(positionParam->value) &&
                   std::get<double>(positionParam->value) == 0.0 &&
                   positionParam->lastEditedActorName == "steward" &&
                   zoomParam != effect.params.end() &&
                   std::holds_alternative<double>(zoomParam->value) &&
                   std::get<double>(zoomParam->value) == 1.6 &&
                   zoomParam->lastEditedActorName == "desktop";
          }
        );
      }
    );
    const bool hasTunedClipTintEffect = std::any_of(
      viewModel.value().timeline.effectGraphs.begin(),
      viewModel.value().timeline.effectGraphs.end(),
      [](const grapple::app::AppEffectGraphRow& graph) {
        return std::any_of(
          graph.effects.begin(),
          graph.effects.end(),
          [](const grapple::app::AppEffectRow& effect) {
            if (effect.entrypoint != grapple::effects::builtin_effect::ClipTintEntrypoint ||
                effect.params.size() != 2) {
              return false;
            }
            const auto colorParam = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::ClipTintColorParam;
              }
            );
            const auto amountParam = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::ClipTintAmountParam;
              }
            );
            return colorParam != effect.params.end() &&
                   std::holds_alternative<grapple::foundation::Vec3>(colorParam->value) &&
                   std::get<grapple::foundation::Vec3>(colorParam->value) == grapple::foundation::Vec3{0.2, 1.0, 0.35} &&
                   colorParam->lastEditedActorName == "desktop" &&
                   amountParam != effect.params.end() &&
                   std::holds_alternative<double>(amountParam->value) &&
                   std::get<double>(amountParam->value) == 0.6 &&
                   amountParam->lastEditedActorName == "steward";
          }
        );
      }
    );

    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "textClips=" << viewModel.value().timeline.textClips.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    std::cout << "effects=" << viewModel.value().timeline.effectCount << '\n';
    std::cout << "evaluatedTunedPreview=" << (hasEvaluatedTunedPreview ? "true" : "false") << '\n';
    std::cout << "previewPixelsChanged=" << (previewPixelsChanged ? "true" : "false") << '\n';
    std::cout << "recentEdits=" << stewardRecentEdits << '\n';
    std::cout << "selectedRecentEdit=" << stewardSelectedRecentEdit << '\n';
    std::cout << "selectedRecentEditText=" << stewardSelectedRecentEditText << '\n';
    if (selectedAfterRecentEdit.has_value()) {
      std::cout << "selectedAfterRecentEdit=" << selectedAfterRecentEdit->value() << '\n';
    }
    std::cout << "exists=" << (exists ? "true" : "false") << '\n';
    std::cout << "size=" << size << '\n';
    std::cout << "encodedResolution=" << encodedResolution.value().width << "x" << encodedResolution.value().height << '\n';
    std::cout << "reopenedRevision=" << reopenedViewModel.value().project.revision.value() << '\n';
    std::cout << "reopenedRuns=" << reopenedConversation.runs.size() << '\n';
    std::cout << "reopenedProductLoopMatches=" << (reopenedProductLoopMatches ? "true" : "false") << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "stewardIntent=" << stewardIntent << '\n';
    std::cout << "stewardActionAfterSampleStart=" << stewardActionAfterSampleStart << '\n';
    std::cout << "stewardActionEnabledAfterSampleStart=" << (stewardActionEnabledAfterSampleStart ? "true" : "false") << '\n';
    std::cout << "addMediaActionEnabledAfterSampleStart=" << (addMediaActionEnabledAfterSampleStart ? "true" : "false") << '\n';
    std::cout << "exportActionEnabledAfterMediaPlacement=" << (exportActionEnabledAfterMediaPlacement ? "true" : "false") << '\n';
    std::cout << "stewardAction=" << stewardActionText << '\n';
    std::cout << "stewardActionEnabled=" << (stewardActionEnabled ? "true" : "false") << '\n';
    std::cout << "clipTintPrimaryAction=" << clipTintPrimaryActionText << '\n';
    std::cout << "clipTintPrimaryActionEnabled=" << (clipTintPrimaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "clipTintUpdatePrimaryAction=" << clipTintUpdatePrimaryActionText << '\n';
    std::cout << "clipTintUpdatePrimaryActionEnabled=" << (clipTintUpdatePrimaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "textCreatePrimaryAction=" << textCreatePrimaryActionText << '\n';
    std::cout << "textCreatePrimaryActionEnabled=" << (textCreatePrimaryActionEnabled ? "true" : "false") << '\n';
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
    std::cout << "effectParamPanel=" << effectParamPanel << '\n';
    std::cout << "exportStatus=" << exportStatus << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().assets.count == 1 &&
           viewModel.value().timeline.clips.size() == 1 &&
           viewModel.value().timeline.textClips.size() == 1 &&
           viewModel.value().timeline.textClips.front().text == "MVP Opening" &&
           viewModel.value().timeline.textClips.front().style.fontSize == 72.0 &&
           viewModel.value().timeline.textClips.front().transform.opacity == 0.75 &&
           viewModel.value().timeline.cameras.size() == 1 &&
           viewModel.value().timeline.effectCount == 2 &&
           hasTunedEditableEffect &&
           hasTunedClipTintEffect &&
           hasEvaluatedTunedPreview &&
           previewPixelsChanged &&
           reopenedProductLoopMatches &&
           stewardActionAfterSampleStart == "Choose Or Type Request" &&
           !stewardActionEnabledAfterSampleStart &&
           !addMediaActionEnabledAfterSampleStart &&
           exportActionEnabledAfterMediaPlacement &&
           stewardRecentEdits == 8 &&
           stewardSelectedRecentEdit == 0 &&
           stewardSelectedRecentEditText.find("Recenter the subject.") != std::string::npos &&
           stewardSelectedRecentEditText.find("Camera Transform on Camera") != std::string::npos &&
           stewardSelectedRecentEditText.find("Position X=0") != std::string::npos &&
           selectedAfterRecentEdit.has_value() &&
           selectedAfterRecentEdit.value() == viewModel.value().timeline.cameras.front().sourceNodeId &&
           steward.find("1 assets | 1 clips | 1 cameras | 2 editable effects") != std::string::npos &&
           steward.find("Next: type the camera edit request, then apply it to the exposed controls.") != std::string::npos &&
           steward.find("Latest result: Camera Transform on Camera (rev_14)") != std::string::npos &&
           steward.find("Controls changed: Position X=0") != std::string::npos &&
           steward.find("Latest request: Recenter the subject.") != std::string::npos &&
           steward.find("Camera target: Camera") != std::string::npos &&
           steward.find("Applied edits: select one to inspect its target.") != std::string::npos &&
           steward.find("Place Asset On Timeline -> succeeded") != std::string::npos &&
           steward.find("- Make clip tint stronger and blue. [succeeded]") != std::string::npos &&
           steward.find("- Tint selected clip red. [succeeded]") != std::string::npos &&
           steward.find("- Recenter the subject. [succeeded]") != std::string::npos &&
           steward.find("- Make the subject bigger. [succeeded]") != std::string::npos &&
           steward.find("Update Effect Parameter -> succeeded") != std::string::npos &&
           stewardIntent.empty() &&
           stewardActionText == "Type Request To Apply Camera Controls" &&
           !stewardActionEnabled &&
           clipTintPrimaryActionText == "Apply Request To Clip" &&
           clipTintPrimaryActionEnabled &&
           clipTintUpdatePrimaryActionText == "Apply Request To Clip" &&
           clipTintUpdatePrimaryActionEnabled &&
           textCreatePrimaryActionText == "Create Text Clip" &&
           textCreatePrimaryActionEnabled &&
           effectParamTitle == "Camera Transform on Camera" &&
           inspector.find("Position X (position_x)=0") != std::string::npos &&
           inspector.find("Zoom (zoom)=1.6") != std::string::npos &&
           inspector.find("Zoom (zoom)=1.6 [0.25..4, step 0.01] last changed by desktop") != std::string::npos &&
           effectParamPanel.find("Position X") != std::string::npos &&
           effectParamPanel.find("Zoom") != std::string::npos &&
           effectParamPanel.find("Last changed by desktop at ") != std::string::npos &&
           exportStatus.find("Export complete: 100 frames, plan " + tunedPreviewFrame.value().frame.renderPlanHash.toHex().substr(0, 8)) != std::string::npos &&
           exportStatus.find(outputPath.string()) != std::string::npos &&
           log.find("Imported starter-gradient") != std::string::npos &&
           log.find("Added selected media to timeline") == std::string::npos &&
           log.find("Steward added selected media to timeline") != std::string::npos &&
           log.find("Steward created clip tint controls") != std::string::npos &&
           log.find("Steward adjusted clip tint controls") != std::string::npos &&
           log.find("Updated effect parameter color") != std::string::npos &&
           log.find("Steward created text clip") != std::string::npos &&
           log.find("Updated text clip") != std::string::npos &&
           log.find("Updated effect parameter zoom") != std::string::npos &&
           log.find("Steward applied camera edit") != std::string::npos &&
           log.find("Steward adjusted camera controls") != std::string::npos &&
           log.find(expectedExportProvenance) != std::string::npos &&
           exists &&
           size > 0U &&
           encodedResolution.value() == grapple::foundation::Resolution{320, 180}
      ? 0
      : 1;
  }

  if (stewardClipTransformSmoke) {
    window.show();
    app.processEvents();
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    window.clickStewardPrimaryAction();
    const auto beforeTransformViewModel = workspace.value().project().buildViewModel();
    if (!beforeTransformViewModel) {
      printError(beforeTransformViewModel.error());
      return 1;
    }
    if (beforeTransformViewModel.value().timeline.clips.empty()) {
      std::cerr << "Steward clip transform smoke requires a clip before transform.\n";
      return 1;
    }
    const auto clipBeforeTransform = beforeTransformViewModel.value().timeline.clips.front().transform;
    const std::string primaryActionTextBeforeIntent = window.stewardPrimaryActionText();
    const std::string stewardIntentPlaceholderBeforeIntent = window.stewardIntentPlaceholder();
    const bool primaryActionEnabledBeforeIntent = window.stewardPrimaryActionEnabled();
    window.setStewardIntent("Tint selected clip red.");
    const std::string primaryActionTextForTint = window.stewardPrimaryActionText();
    const bool primaryActionEnabledForTint = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Make clip tint stronger and blue.");
    const std::string primaryActionTextForTintUpdate = window.stewardPrimaryActionText();
    const bool primaryActionEnabledForTintUpdate = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    window.setEffectParamVec3ControlValue(
      grapple::effects::builtin_effect::ClipTintColorParam,
      grapple::foundation::Vec3{0.2, 1.0, 0.35}
    );
    window.setStewardIntent("Brighten selected clip.");
    const std::string primaryActionTextForExposure = window.stewardPrimaryActionText();
    const bool primaryActionEnabledForExposure = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Make selected clip darker.");
    const std::string primaryActionTextForExposureUpdate = window.stewardPrimaryActionText();
    const bool primaryActionEnabledForExposureUpdate = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Move selected clip right, rotate slightly left, make it smaller, make it faster, and make it invisible.");
    const std::string primaryActionTextAfterIntent = window.stewardPrimaryActionText();
    const bool primaryActionEnabledAfterIntent = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    if (viewModel.value().timeline.clips.empty()) {
      std::cerr << "Steward clip transform smoke requires a clip.\n";
      return 1;
    }
    const auto& clip = viewModel.value().timeline.clips.front();
    const auto renderedFrame = workspace.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      workspace.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!renderedFrame) {
      printError(renderedFrame.error());
      return 1;
    }
    const std::string steward = window.stewardContents();
    const std::string stewardIntent = window.stewardIntent();
    const std::string inspector = window.inspectorContents();
    const std::string effectParamTitle = window.effectParamTitleText();
    const std::string effectParamPanel = window.effectParamPanelText();
    const std::string log = window.logContents();
    const bool clipTintUpdated = std::any_of(
      viewModel.value().timeline.effectGraphs.begin(),
      viewModel.value().timeline.effectGraphs.end(),
      [](const grapple::app::AppEffectGraphRow& graph) {
        return std::any_of(
          graph.effects.begin(),
          graph.effects.end(),
          [](const grapple::app::AppEffectRow& effect) {
            if (effect.entrypoint != grapple::effects::builtin_effect::ClipTintEntrypoint ||
                effect.params.size() != 2) {
              return false;
            }
            const auto colorParam = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::ClipTintColorParam;
              }
            );
            const auto amountParam = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::ClipTintAmountParam;
              }
            );
            return colorParam != effect.params.end() &&
                   std::holds_alternative<grapple::foundation::Vec3>(colorParam->value) &&
                   std::get<grapple::foundation::Vec3>(colorParam->value) == grapple::foundation::Vec3{0.2, 1.0, 0.35} &&
                   colorParam->lastEditedActorName == "desktop" &&
                   amountParam != effect.params.end() &&
                   std::holds_alternative<double>(amountParam->value) &&
                   std::get<double>(amountParam->value) == 0.6 &&
                   amountParam->lastEditedActorName == "steward";
          }
        );
      }
    );
    const bool clipExposureUpdated = std::any_of(
      viewModel.value().timeline.effectGraphs.begin(),
      viewModel.value().timeline.effectGraphs.end(),
      [](const grapple::app::AppEffectGraphRow& graph) {
        return std::any_of(
          graph.effects.begin(),
          graph.effects.end(),
          [](const grapple::app::AppEffectRow& effect) {
            if (effect.entrypoint != grapple::effects::builtin_effect::ClipExposureEntrypoint ||
                effect.params.size() != 1) {
              return false;
            }
            const auto exposureParam = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::ClipExposureParam;
              }
            );
            return exposureParam != effect.params.end() &&
                   std::holds_alternative<double>(exposureParam->value) &&
                   std::get<double>(exposureParam->value) == -0.35 &&
                   exposureParam->lastEditedActorName == "steward";
          }
        );
      }
    );
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "initialClipPositionX=" << clipBeforeTransform.position.x << '\n';
    std::cout << "initialClipScaleX=" << clipBeforeTransform.scale.x << '\n';
    std::cout << "initialClipRotation=" << clipBeforeTransform.rotationDegrees << '\n';
    std::cout << "initialClipOpacity=" << clipBeforeTransform.opacity << '\n';
    std::cout << "clipPositionX=" << clip.transform.position.x << '\n';
    std::cout << "clipPositionY=" << clip.transform.position.y << '\n';
    std::cout << "clipScaleX=" << clip.transform.scale.x << '\n';
    std::cout << "clipScaleY=" << clip.transform.scale.y << '\n';
    std::cout << "clipRotation=" << clip.transform.rotationDegrees << '\n';
    std::cout << "clipOpacity=" << clip.transform.opacity << '\n';
    std::cout << "clipPlaybackRate=" << clip.playbackRate << '\n';
    std::cout << "renderedMediaFrames=" << renderedFrame.value().frame.mediaFrames.size() << '\n';
    std::cout << "primaryActionTextBeforeIntent=" << primaryActionTextBeforeIntent << '\n';
    std::cout << "stewardIntentPlaceholderBeforeIntent=" << stewardIntentPlaceholderBeforeIntent << '\n';
    std::cout << "primaryActionEnabledBeforeIntent=" << (primaryActionEnabledBeforeIntent ? "true" : "false") << '\n';
    std::cout << "primaryActionTextForTint=" << primaryActionTextForTint << '\n';
    std::cout << "primaryActionEnabledForTint=" << (primaryActionEnabledForTint ? "true" : "false") << '\n';
    std::cout << "primaryActionTextForTintUpdate=" << primaryActionTextForTintUpdate << '\n';
    std::cout << "primaryActionEnabledForTintUpdate=" << (primaryActionEnabledForTintUpdate ? "true" : "false") << '\n';
    std::cout << "primaryActionTextForExposure=" << primaryActionTextForExposure << '\n';
    std::cout << "primaryActionEnabledForExposure=" << (primaryActionEnabledForExposure ? "true" : "false") << '\n';
    std::cout << "primaryActionTextForExposureUpdate=" << primaryActionTextForExposureUpdate << '\n';
    std::cout << "primaryActionEnabledForExposureUpdate=" << (primaryActionEnabledForExposureUpdate ? "true" : "false") << '\n';
    std::cout << "primaryActionTextAfterIntent=" << primaryActionTextAfterIntent << '\n';
    std::cout << "primaryActionEnabledAfterIntent=" << (primaryActionEnabledAfterIntent ? "true" : "false") << '\n';
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
    std::cout << "effectParamPanel=" << effectParamPanel << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "stewardIntent=" << stewardIntent << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_10"} &&
           viewModel.value().timeline.effectCount == 2 &&
           viewModel.value().timeline.effectGraphs.size() == 1 &&
           viewModel.value().timeline.effectGraphs[0].effects.size() == 2 &&
           viewModel.value().timeline.effectGraphs[0].effects[0].displayName == grapple::effects::builtin_effect::ClipTintDisplayName &&
           clipTintUpdated &&
           clipExposureUpdated &&
           renderedFrame.value().frame.sourceRevision == viewModel.value().project.revision &&
           renderedFrame.value().frame.mediaFrames.size() == 1 &&
           renderedFrame.value().frame.mediaFrames[0].tintColor.has_value() &&
           renderedFrame.value().frame.mediaFrames[0].tintColor.value() == grapple::foundation::Vec3{0.2, 1.0, 0.35} &&
           renderedFrame.value().frame.mediaFrames[0].tintAmount == 0.6 &&
           renderedFrame.value().frame.mediaFrames[0].exposure == -0.35 &&
           clip.transform.position.x == clipBeforeTransform.position.x + 0.25 &&
           clip.transform.position.y == clipBeforeTransform.position.y &&
           clip.transform.scale.x == clipBeforeTransform.scale.x * 0.75 &&
           clip.transform.scale.y == clipBeforeTransform.scale.y * 0.75 &&
           clip.transform.rotationDegrees == clipBeforeTransform.rotationDegrees - 7.5 &&
           clip.transform.opacity == 0.0 &&
           clip.playbackRate == 1.25 &&
           primaryActionTextBeforeIntent == "Choose Or Type Request" &&
           stewardIntentPlaceholderBeforeIntent.find("tint selected clip red") != std::string::npos &&
           stewardIntentPlaceholderBeforeIntent.find("brighten selected clip") != std::string::npos &&
           stewardIntentPlaceholderBeforeIntent.find("speed up selected clip") != std::string::npos &&
           stewardIntentPlaceholderBeforeIntent.find("delete selected clip") != std::string::npos &&
           !primaryActionEnabledBeforeIntent &&
           primaryActionTextForTint == "Apply Request To Clip" &&
           primaryActionEnabledForTint &&
           primaryActionTextForTintUpdate == "Apply Request To Clip" &&
           primaryActionEnabledForTintUpdate &&
           primaryActionTextForExposure == "Apply Request To Clip" &&
           primaryActionEnabledForExposure &&
           primaryActionTextForExposureUpdate == "Apply Request To Clip" &&
           primaryActionEnabledForExposureUpdate &&
           primaryActionTextAfterIntent == "Apply Request To Clip" &&
           primaryActionEnabledAfterIntent &&
           stewardIntent.empty() &&
           steward.find("Next: type a selected clip request, or type a camera request.") != std::string::npos &&
           steward.find("Clip target: starter-gradient") != std::string::npos &&
           steward.find("Clip route: mention tint/color for editable Clip Tint") != std::string::npos &&
           steward.find("exposure/brighten/darken for editable Clip Exposure") != std::string::npos &&
           steward.find("Tint selected clip red.") != std::string::npos &&
           steward.find("Make clip tint stronger and blue.") != std::string::npos &&
           steward.find("Brighten selected clip.") != std::string::npos &&
           steward.find("Make selected clip darker.") != std::string::npos &&
           steward.find("Rotation=-7.5") != std::string::npos &&
           inspector.find("Rotation: -7.50") != std::string::npos &&
           inspector.find("Speed: 1.25x") != std::string::npos &&
           inspector.find("Opacity: 0.00") != std::string::npos &&
           effectParamTitle == "Clip Tint on starter-gradient" &&
           effectParamPanel.find("Tint Color") != std::string::npos &&
           effectParamPanel.find("\nR\n") != std::string::npos &&
           effectParamPanel.find("\nG\n") != std::string::npos &&
           effectParamPanel.find("\nB\n") != std::string::npos &&
           effectParamPanel.find("Tint Amount") != std::string::npos &&
           effectParamPanel.find("Clip Exposure on starter-gradient") != std::string::npos &&
           effectParamPanel.find("Exposure") != std::string::npos &&
           steward.find("Create Effect Node -> succeeded") != std::string::npos &&
           steward.find("Update Effect Parameter -> succeeded") != std::string::npos &&
           steward.find("Update Clip Transform -> succeeded") != std::string::npos &&
           steward.find("Update Clip Playback Rate -> succeeded") != std::string::npos &&
           log.find("Steward created clip tint controls") != std::string::npos &&
           log.find("Steward adjusted clip tint controls") != std::string::npos &&
           log.find("Steward created clip exposure controls") != std::string::npos &&
           log.find("Steward adjusted clip exposure controls") != std::string::npos &&
           log.find("Updated effect parameter color") != std::string::npos &&
           log.find("Steward edited selected clip") != std::string::npos
      ? 0
      : 1;
  }

  if (stewardDeleteClipSmoke) {
    window.show();
    app.processEvents();
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Delete selected clip.");
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto conversation = workspace.value().steward().conversationState();
    const std::string steward = window.stewardContents();
    const std::string log = window.logContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    std::cout << "selectedNode=" << (window.selectedNodeId().has_value() ? window.selectedNodeId()->value() : "none") << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_3"} &&
           viewModel.value().timeline.clips.empty() &&
           !window.selectedNodeId().has_value() &&
           window.stewardIntent().empty() &&
           conversation.runs.size() == 2 &&
           conversation.runs[0].toolCalls.size() == 1 &&
           conversation.runs[0].toolCalls[0].toolSerializedId == "timeline.place_asset" &&
           conversation.runs[1].toolCalls.size() == 1 &&
           conversation.runs[1].toolCalls[0].toolSerializedId == "timeline.delete_clip" &&
           steward.find("Clip Delete") != std::string::npos &&
           steward.find("Delete selected clip.") != std::string::npos &&
           steward.find("Delete Timeline Clip -> succeeded") != std::string::npos &&
           log.find("Steward added selected media to timeline") != std::string::npos &&
           log.find("Steward deleted selected clip") != std::string::npos
      ? 0
      : 1;
  }

  if (stewardUndoSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Create editable camera controls.");
    window.clickStewardPrimaryAction();
    const auto afterCreate = workspace.value().project().buildViewModel();
    if (!afterCreate) {
      printError(afterCreate.error());
      return 1;
    }

    window.setStewardIntent("Undo last edit.");
    const bool undoActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto afterUndo = workspace.value().project().buildViewModel();
    if (!afterUndo) {
      printError(afterUndo.error());
      return 1;
    }

    const auto& commandsAfterUndo = workspace.value().project().packageState().commandLog.records();
    const bool undoCommittedThroughSteward =
      !commandsAfterUndo.empty() &&
      commandsAfterUndo.back().serializedName == "project.restore_snapshot" &&
      commandsAfterUndo.back().sourceKind == "agent" &&
      commandsAfterUndo.back().sourceActorName == "steward" &&
      commandsAfterUndo.back().sourceRunId == grapple::foundation::RunId{"run_steward_2"};

    window.setStewardIntent("Redo last edit.");
    const bool redoActionEnabled = window.stewardPrimaryActionEnabled();
    window.clickStewardPrimaryAction();
    const auto afterRedo = workspace.value().project().buildViewModel();
    if (!afterRedo) {
      printError(afterRedo.error());
      return 1;
    }

    const auto conversation = workspace.value().steward().conversationState();
    const std::string steward = window.stewardContents();
    const std::string inspector = window.inspectorContents();
    const std::string log = window.logContents();
    const auto& commands = workspace.value().project().packageState().commandLog.records();
    std::cout << "afterCreateRevision=" << afterCreate.value().project.revision.value() << '\n';
    std::cout << "afterCreateEffectGraphs=" << afterCreate.value().timeline.effectGraphs.size() << '\n';
    std::cout << "undoActionEnabled=" << (undoActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterUndoRevision=" << afterUndo.value().project.revision.value() << '\n';
    std::cout << "afterUndoEffectGraphs=" << afterUndo.value().timeline.effectGraphs.size() << '\n';
    std::cout << "afterUndoStewardEdits=" << afterUndo.value().steward.edits.size() << '\n';
    std::cout << "redoActionEnabled=" << (redoActionEnabled ? "true" : "false") << '\n';
    std::cout << "afterRedoRevision=" << afterRedo.value().project.revision.value() << '\n';
    std::cout << "afterRedoEffectGraphs=" << afterRedo.value().timeline.effectGraphs.size() << '\n';
    std::cout << "afterRedoStewardEdits=" << afterRedo.value().steward.edits.size() << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    if (!commands.empty()) {
      std::cout << "lastCommand=" << commands.back().serializedName << '\n';
      std::cout << "lastCommandSource=" << commands.back().sourceKind << ':' << commands.back().sourceActorName << '\n';
    }
    std::cout << "steward=" << steward << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "log=" << log << '\n';
    return afterCreate.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           afterCreate.value().timeline.effectGraphs.size() == 1 &&
           undoActionEnabled &&
           afterUndo.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           afterUndo.value().timeline.effectGraphs.empty() &&
           afterUndo.value().steward.edits.empty() &&
           redoActionEnabled &&
           afterRedo.value().project.revision == grapple::foundation::RevisionId{"rev_8"} &&
           afterRedo.value().timeline.effectGraphs.size() == 1 &&
           afterRedo.value().steward.edits.size() == 2 &&
           window.stewardIntent().empty() &&
           conversation.runs.size() == 3 &&
           conversation.runs[0].toolCalls.size() == 1 &&
           conversation.runs[0].toolCalls[0].toolSerializedId == "effect.create_node" &&
           conversation.runs[1].toolCalls.empty() &&
           conversation.runs[2].toolCalls.empty() &&
           undoCommittedThroughSteward &&
           !commands.empty() &&
           commands.back().serializedName == "project.create_effect" &&
           commands.back().sourceKind == "agent" &&
           commands.back().sourceActorName == "steward" &&
           commands.back().sourceRunId == grapple::foundation::RunId{"run_steward_3"} &&
           steward.find("- Undo last edit. [succeeded]") != std::string::npos &&
           steward.find("Undid the last committed project edit.") != std::string::npos &&
           steward.find("- Redo last edit. [succeeded]") != std::string::npos &&
           steward.find("Redid the last undone project edit.") != std::string::npos &&
           inspector.find("No selection") != std::string::npos &&
           log.find("Steward undid last edit") != std::string::npos &&
           log.find("Steward redid last edit") != std::string::npos
      ? 0
      : 1;
  }

  if (stewardDeleteCameraControlsSmoke) {
    window.show();
    app.processEvents();
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Create editable camera controls.");
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Remove camera controls.");
    window.clickStewardPrimaryAction();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto conversation = workspace.value().steward().conversationState();
    const std::string steward = window.stewardContents();
    const std::string inspector = window.inspectorContents();
    const std::string log = window.logContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "effectGraphs=" << viewModel.value().timeline.effectGraphs.size() << '\n';
    std::cout << "runs=" << conversation.runs.size() << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_4"} &&
           viewModel.value().timeline.effectGraphs.empty() &&
           viewModel.value().steward.edits.size() == 3 &&
           viewModel.value().steward.edits[2].editName == "Camera Transform Delete" &&
           viewModel.value().steward.edits[2].controlSummary == "Deleted" &&
           window.stewardIntent().empty() &&
           conversation.runs.size() == 3 &&
           conversation.runs[1].toolCalls.size() == 1 &&
           conversation.runs[1].toolCalls[0].toolSerializedId == "effect.create_node" &&
           conversation.runs[2].toolCalls.size() == 1 &&
           conversation.runs[2].toolCalls[0].toolSerializedId == "effect.delete_node" &&
           steward.find("Camera Transform Delete") != std::string::npos &&
           steward.find("Delete Effect Node -> succeeded") != std::string::npos &&
           inspector.find("Camera\nName: Camera") != std::string::npos &&
           inspector.find("No effects attached.") != std::string::npos &&
           log.find("Steward applied camera edit") != std::string::npos &&
           log.find("Steward deleted camera controls") != std::string::npos
      ? 0
      : 1;
  }

  if (editSaveSmoke) {
    window.importMediaFile(grapple::foundation::FilePath{starterVideoPath.string()});
    window.addSelectedMediaToTimeline();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Persist editable camera controls.");
    window.clickStewardPrimaryAction();
    window.setSelectedTargetNumericEffectParam(grapple::effects::builtin_effect::PositionXParam, 0.25);
    const std::string dirtyHeader = window.projectHeaderText();
    const bool dirtySaveActionEnabled = window.saveActionEnabled();
    const auto write = workspace.value().writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }
    auto reopened = grapple::app::NativeWorkspaceSession::openPackageRoot(grapple::foundation::FilePath{packageRoot.string()});
    if (!reopened) {
      printError(reopened.error());
      return 1;
    }
    const auto viewModel = reopened.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const auto previewRefresh = reopened.value().preview().refreshFromProject();
    if (!previewRefresh) {
      printError(previewRefresh.error());
      return 1;
    }
    const auto previewFrame = reopened.value().preview().renderFrame(grapple::render::RenderFrameRequest{
      reopened.value().preview().state().playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!previewFrame) {
      printError(previewFrame.error());
      return 1;
    }
    const auto conversation = reopened.value().steward().conversationState();
    const bool reopenedTunedEffect = std::any_of(
      viewModel.value().timeline.effectGraphs.begin(),
      viewModel.value().timeline.effectGraphs.end(),
      [](const grapple::app::AppEffectGraphRow& graph) {
        return std::any_of(
          graph.effects.begin(),
          graph.effects.end(),
          [](const grapple::app::AppEffectRow& effect) {
            const auto param = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::effects::builtin_effect::PositionXParam;
              }
            );
            return effect.cameraTransformEffect &&
                   effect.createdActorName == "steward" &&
                   effect.createdIntent == "Persist editable camera controls." &&
                   param != effect.params.end() &&
                   std::holds_alternative<double>(param->value) &&
                   std::get<double>(param->value) == 0.25 &&
                   param->lastEditedRevision.has_value() &&
                   param->lastEditedActorName == "desktop";
          }
        );
      }
    );
    const bool reopenedPreviewTuned =
      previewFrame.value().frame.sourceRevision == viewModel.value().project.revision &&
      previewFrame.value().frame.cameras.size() == 1 &&
      previewFrame.value().frame.cameras.front().state.transform.position.x == 0.25;
    const bool stewardContextRestored =
      conversation.diagnostics.empty() &&
      conversation.runs.size() == 1 &&
      conversation.runs[0].status == grapple::agent::AgentRunStatus::Succeeded &&
      conversation.runs[0].toolCalls.size() == 1 &&
      conversation.runs[0].toolCalls[0].toolSerializedId == "effect.create_node" &&
      conversation.runs[0].toolCalls[0].observedRevision == grapple::foundation::RevisionId{"rev_8"};
    const std::filesystem::path reopenedExportPath = smokeRoot / "reopened-export.avi";
    std::filesystem::remove(reopenedExportPath);
    const auto reopenedPlan = reopened.value().project().buildRenderPlan();
    if (!reopenedPlan) {
      printError(reopenedPlan.error());
      return 1;
    }
    const auto reopenedExport = reopened.value().exportSession().renderPlanToVideo(
      reopenedPlan.value().plan,
      grapple::render::ExportSettings{
        grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, grapple::foundation::TimeSeconds{1.0}},
        grapple::foundation::FrameRate{1, 1},
        grapple::foundation::Resolution{320, 180},
        grapple::render::Codec{"mjpeg"},
        grapple::render::RenderQuality::Final,
        grapple::foundation::FilePath{reopenedExportPath.string()}
      }
    );
    if (!reopenedExport) {
      printError(reopenedExport.error());
      return 1;
    }
    const bool reopenedExportExists = std::filesystem::exists(reopenedExportPath);
    const auto reopenedExportSize = reopenedExportExists ? std::filesystem::file_size(reopenedExportPath) : 0U;
    const bool reopenedExportMatchesPlan =
      reopenedExport.value().sourceRevision == viewModel.value().project.revision &&
      reopenedExport.value().renderPlanHash == previewFrame.value().frame.renderPlanHash &&
      reopenedExport.value().framesEvaluated == 1 &&
      reopenedExportExists &&
      reopenedExportSize > 0U;
    const std::filesystem::path saveAsRoot = smokeRoot / "desktop-save-as-package";
    std::filesystem::remove_all(saveAsRoot);
    window.savePackageAs(grapple::foundation::FilePath{saveAsRoot.string()});
    const std::string saveAsHeader = window.projectHeaderText();
    const bool savedSaveActionEnabled = window.saveActionEnabled();
    auto reopenedSaveAs = grapple::app::NativeWorkspaceSession::openPackageRoot(grapple::foundation::FilePath{saveAsRoot.string()});
    if (!reopenedSaveAs) {
      printError(reopenedSaveAs.error());
      return 1;
    }
    const auto saveAsViewModel = reopenedSaveAs.value().project().buildViewModel();
    if (!saveAsViewModel) {
      printError(saveAsViewModel.error());
      return 1;
    }
    const auto saveAsConversation = reopenedSaveAs.value().steward().conversationState();
    bool saveAsPackageLocalMediaCopied = false;
    bool saveAsPackageLocalMediaMissing = false;
    for (const grapple::app::AppAssetRow& asset : saveAsViewModel.value().assets.rows) {
      const std::filesystem::path sourcePath{asset.sourcePath.value};
      if (sourcePath.is_absolute()) {
        continue;
      }
      saveAsPackageLocalMediaCopied = true;
      if (!std::filesystem::exists(saveAsRoot / sourcePath)) {
        saveAsPackageLocalMediaMissing = true;
      }
      if (asset.thumbnailPath.has_value() &&
          !std::filesystem::exists(saveAsRoot / asset.thumbnailPath->value)) {
        saveAsPackageLocalMediaMissing = true;
      }
    }
    const bool saveAsRestored =
      saveAsViewModel.value().project.revision == grapple::foundation::RevisionId{"rev_9"} &&
      saveAsViewModel.value().timeline.effectCount == 1 &&
      saveAsConversation.diagnostics.empty() &&
      saveAsConversation.runs.size() == 1 &&
      dirtyHeader.find("Unsaved") != std::string::npos &&
      dirtySaveActionEnabled &&
      saveAsHeader.find("Desktop Demo") != std::string::npos &&
      saveAsHeader.find("desktop-save-as-package") != std::string::npos &&
      saveAsHeader.find("Saved") != std::string::npos &&
      saveAsHeader.find("Unsaved") == std::string::npos &&
      !savedSaveActionEnabled &&
      std::filesystem::exists(saveAsRoot / "manifest.json") &&
      std::filesystem::exists(saveAsRoot / "agent/runs.json") &&
      std::filesystem::exists(saveAsRoot / "agent/events.json") &&
      saveAsPackageLocalMediaCopied &&
      !saveAsPackageLocalMediaMissing;
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "effects=" << viewModel.value().timeline.effectCount << '\n';
    std::cout << "commands=" << reopened.value().project().packageState().commandLog.records().size() << '\n';
    std::cout << "reopenedTunedEffect=" << (reopenedTunedEffect ? "true" : "false") << '\n';
    std::cout << "reopenedPreviewTuned=" << (reopenedPreviewTuned ? "true" : "false") << '\n';
    std::cout << "stewardContextRestored=" << (stewardContextRestored ? "true" : "false") << '\n';
    std::cout << "reopenedExportMatchesPlan=" << (reopenedExportMatchesPlan ? "true" : "false") << '\n';
    std::cout << "reopenedExportSize=" << reopenedExportSize << '\n';
    std::cout << "saveAsPackageLocalMediaCopied=" << (saveAsPackageLocalMediaCopied ? "true" : "false") << '\n';
    std::cout << "saveAsPackageLocalMediaMissing=" << (saveAsPackageLocalMediaMissing ? "true" : "false") << '\n';
    std::cout << "dirtyHeader=" << dirtyHeader << '\n';
    std::cout << "dirtySaveActionEnabled=" << (dirtySaveActionEnabled ? "true" : "false") << '\n';
    std::cout << "saveAsHeader=" << saveAsHeader << '\n';
    std::cout << "savedSaveActionEnabled=" << (savedSaveActionEnabled ? "true" : "false") << '\n';
    std::cout << "saveAsRestored=" << (saveAsRestored ? "true" : "false") << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_9"} &&
           viewModel.value().assets.count == 2 &&
           viewModel.value().timeline.clips.size() == 2 &&
           viewModel.value().timeline.effectCount == 1 &&
           reopened.value().project().packageState().commandLog.records().size() == 9 &&
           reopenedTunedEffect &&
           reopenedPreviewTuned &&
           stewardContextRestored &&
           reopenedExportMatchesPlan &&
           saveAsRestored
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

  if (effectScreenshotPath.has_value()) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Center the subject with editable camera controls.");
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Move the camera framing right.");
    window.clickStewardPrimaryAction();
    window.setStewardIntent("Zoom in.");
    window.clickStewardPrimaryAction();
    app.processEvents();
    const QPixmap pixmap = window.grab();
    if (!pixmap.save(QString::fromStdString(*effectScreenshotPath))) {
      std::cerr << "Could not write effect screenshot: " << *effectScreenshotPath << '\n';
      return 1;
    }
    return 0;
  }

  window.show();
  return app.exec();
}
