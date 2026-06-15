#include "DesktopApp.hpp"
#include "DesktopWindow.hpp"

#include <DemoProject.hpp>

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/render/RenderQuality.hpp>
#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>

#include <QApplication>
#include <QPixmap>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
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
  bool emptyAddTrackSmoke = false;
  bool emptyAddCameraSmoke = false;
  bool updateCameraSmoke = false;
  bool addNoteSmoke = false;
  bool updateNoteSmoke = false;
  bool moveClipSmoke = false;
  bool trimClipSmoke = false;
  bool nudgeClipSmoke = false;
  bool undoRedoSmoke = false;
  bool addEffectSmoke = false;
  bool setEffectParamSmoke = false;
  bool effectKeyframeSmoke = false;
  bool deleteEffectSmoke = false;
  bool deleteSmoke = false;
  bool deleteTrackSmoke = false;
  bool playbackSmoke = false;
  bool openPackageSmoke = false;
  bool editSaveSmoke = false;
  bool exportSettingsSmoke = false;
  bool productLoopSmoke = false;
  bool emptyLaunchSmoke = false;
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
    } else if (argument == "--nudge-clip-smoke") {
      nudgeClipSmoke = true;
    } else if (argument == "--undo-redo-smoke") {
      undoRedoSmoke = true;
    } else if (argument == "--add-effect-smoke") {
      addEffectSmoke = true;
    } else if (argument == "--set-effect-param-smoke") {
      setEffectParamSmoke = true;
    } else if (argument == "--effect-keyframe-smoke") {
      effectKeyframeSmoke = true;
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
    } else if (argument == "--export-settings-smoke") {
      exportSettingsSmoke = true;
    } else if (argument == "--product-loop-smoke") {
      productLoopSmoke = true;
    } else if (argument == "--empty-launch-smoke") {
      emptyLaunchSmoke = true;
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else if (argument == "--effect-screenshot" && index + 1 < argc) {
      effectScreenshotPath = argv[++index];
    } else {
      std::cerr << "Expected --smoke, --mutate-smoke, --seek-smoke, --timeline-seek-smoke, --select-smoke, --select-audio-clip-smoke, --select-audio-track-smoke, --select-camera-smoke, --select-second-camera-smoke, --steward-smoke, --import-smoke, --import-media-types-smoke, --add-video-smoke, --empty-add-video-smoke, --empty-add-track-smoke, --empty-add-camera-smoke, --update-camera-smoke, --add-note-smoke, --update-note-smoke, --move-clip-smoke, --trim-clip-smoke, --nudge-clip-smoke, --undo-redo-smoke, --add-effect-smoke, --set-effect-param-smoke, --effect-keyframe-smoke, --delete-effect-smoke, --delete-smoke, --delete-track-smoke, --playback-smoke, --open-package-smoke, --edit-save-smoke, --export-settings-smoke, --product-loop-smoke, --empty-launch-smoke, --screenshot <path>, or --effect-screenshot <path>.\n";
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
      grapple::storage::CurrentProjectPackageSchemaVersion
    }
  };

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
    nudgeClipSmoke ||
    undoRedoSmoke ||
    addEffectSmoke ||
    setEffectParamSmoke ||
    effectKeyframeSmoke ||
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
    productLoopSmoke;

  if (needsStarterDemoVideo) {
    const auto demoVideo = grapple::demo::ensureStarterDemoVideo();
    if (!demoVideo) {
      printError(demoVideo.error());
      return 1;
    }
  }

  if (populateStarterDemo) {
    const auto populated = populateDemo(session, true);
    if (!populated) {
      printError(populated.error());
      return 1;
    }
  }

  auto workspace = grapple::app::NativeWorkspaceSession::fromProject(std::move(session));
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
    const std::string stewardActionText = window.stewardPrimaryActionText();
    const bool stewardActionEnabled = window.stewardPrimaryActionEnabled();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    std::cout << "stewardAction=" << stewardActionText << '\n';
    std::cout << "stewardActionEnabled=" << (stewardActionEnabled ? "true" : "false") << '\n';
    std::cout << "steward=" << steward << '\n';
    return viewModel.value().assets.count == 0 &&
           viewModel.value().timeline.clips.empty() &&
           viewModel.value().timeline.cameras.empty() &&
           stewardActionText == "Import Media First" &&
           !stewardActionEnabled &&
           steward.find("0 clips | 0 cameras | 0 editable effects") != std::string::npos
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
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "selected=" << selectedNodeId->value() << '\n';
    std::cout << "runs=" << workspace.value().steward().conversationState().runs.size() << '\n';
    return !viewModel.value().timeline.clips.empty() &&
           selectedNodeId.value() == viewModel.value().timeline.clips.front().sourceNodeId &&
           workspace.value().steward().conversationState().runs.empty() &&
           viewModel.value().timeline.effectCount == 0 &&
           log.find("steward.selected_node_not_camera") == std::string::npos
      ? 0
      : 1;
  }

  if (selectAudioClipSmoke) {
    const std::filesystem::path audioPath{"/tmp/grapple-desktop-select-audio.wav"};
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
    const std::filesystem::path audioPath{"/tmp/grapple-desktop-select-audio-track.wav"};
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
    std::cout << "inspector=" << inspector << '\n';
    if (selectedNodeId.has_value()) {
      std::cout << "selected=" << selectedNodeId->value() << '\n';
    }
    std::filesystem::remove(audioPath);
    return selectedNodeId.has_value() &&
           selectedNodeId.value() == viewModel.value().timeline.audioTracks.front().sourceNodeId &&
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
    std::cout << "selected=" << selectedNodeId->value() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return !viewModel.value().timeline.cameras.empty() &&
           selectedNodeId.value() == viewModel.value().timeline.cameras.front().sourceNodeId &&
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
           steward.find("Loop") != std::string::npos &&
           steward.find("Create an editable result") != std::string::npos &&
           steward.find("editable graph") == std::string::npos &&
           steward.find("Editable controls") != std::string::npos &&
           steward.find("- no editable controls yet") != std::string::npos &&
           steward.find("Recent Steward runs") != std::string::npos &&
           steward.find("- no runs yet") != std::string::npos &&
           !viewModel.value().timeline.cameras.empty() &&
           selectedNodeId.has_value() &&
           selectedNodeId.value() == viewModel.value().timeline.cameras.front().sourceNodeId
      ? 0
      : 1;
  }

  if (importSmoke) {
    window.importMediaFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/starter-gradient.avi"});
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
    const std::filesystem::path imagePath{"/tmp/grapple-desktop-import-image.ppm"};
    const std::filesystem::path audioPath{"/tmp/grapple-desktop-import-audio.wav"};
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

    window.importMediaFile(grapple::foundation::FilePath{imagePath.string()});
    window.addSelectedMediaToTimeline();
    window.importMediaFile(grapple::foundation::FilePath{audioPath.string()});
    window.addSelectedMediaToTimeline();
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
    window.importMediaFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/starter-gradient.avi"});
    window.addSelectedMediaToTimeline();
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
    window.importMediaFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/starter-gradient.avi"});
    window.addSelectedMediaToTimeline();
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
    if (window.selectedNodeId().has_value()) {
      std::cout << "selectedNode=" << window.selectedNodeId()->value() << '\n';
    }
    return viewModel.value().assets.count == 1 &&
           viewModel.value().timeline.compositions.size() == 1 &&
           viewModel.value().timeline.layers.size() == 1 &&
           viewModel.value().timeline.cameras.size() == 1 &&
           viewModel.value().timeline.clips.size() == 1 &&
           viewModel.value().timeline.duration.value > 9.9 &&
           window.selectedNodeId().has_value()
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
           stewardActionBefore == "Import Media First" &&
           !stewardActionEnabledBefore &&
           stewardBefore.find("0 clips | 0 cameras | 0 editable effects") != std::string::npos &&
           stewardAfter.find("0 clips | 1 cameras | 0 editable effects") != std::string::npos &&
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

  if (addNoteSmoke) {
    window.addNote();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    const std::string inspector = window.inspectorContents();
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "notes=" << viewModel.value().notes.rows.size() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           viewModel.value().notes.rows.size() == 1 &&
           viewModel.value().notes.rows.front().title == "Note 1" &&
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
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "notes=" << viewModel.value().notes.rows.size() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           viewModel.value().notes.rows.size() == 1 &&
           viewModel.value().notes.rows.front().title == "Updated Note" &&
           viewModel.value().notes.rows.front().markdown == "Updated project note" &&
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

  if (nudgeClipSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    window.setSelectedClipTransformControlValue("clipTransformPositionX", 0.25);
    window.setSelectedClipTransformControlValue("clipTransformPositionY", 0.5);
    window.setSelectedClipTransformControlValue("clipTransformScaleX", 1.25);
    window.setSelectedClipTransformControlValue("clipTransformScaleY", 1.25);
    window.setSelectedClipTransformControlValue("clipTransformOpacity", 0.5);
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
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_10"} &&
           clip.transform.position.x == 0.25 &&
           clip.transform.position.y == 0.5 &&
           clip.transform.scale.x == 1.25 &&
           clip.transform.scale.y == 1.25 &&
           clip.transform.opacity == 0.5 &&
           clip.timelineRange.start == grapple::foundation::TimeSeconds{0.0} &&
           clip.timelineRange.end == grapple::foundation::TimeSeconds{10.0} &&
           clip.sourceRange.start == grapple::foundation::TimeSeconds{0.0} &&
           clip.sourceRange.end == grapple::foundation::TimeSeconds{10.0} &&
           inspector.find("Position: 0.25, 0.50") != std::string::npos &&
           inspector.find("Scale: 1.25, 1.25") != std::string::npos &&
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
        return row.name == grapple::runtime::builtin_effect::PositionXParam;
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
    window.undoLastEdit();
    auto afterUndo = workspace.value().project().buildViewModel();
    if (!afterUndo) {
      printError(afterUndo.error());
      return 1;
    }
    window.redoLastEdit();
    auto afterRedo = workspace.value().project().buildViewModel();
    if (!afterRedo) {
      printError(afterRedo.error());
      return 1;
    }
    std::cout << "afterAddRevision=" << afterAdd.value().project.revision.value() << '\n';
    std::cout << "afterAddLayers=" << afterAdd.value().timeline.layers.size() << '\n';
    std::cout << "afterUndoRevision=" << afterUndo.value().project.revision.value() << '\n';
    std::cout << "afterUndoLayers=" << afterUndo.value().timeline.layers.size() << '\n';
    std::cout << "afterRedoRevision=" << afterRedo.value().project.revision.value() << '\n';
    std::cout << "afterRedoLayers=" << afterRedo.value().timeline.layers.size() << '\n';
    const bool trackUndoRedoOk =
      afterAdd.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
      afterAdd.value().timeline.layers.size() == 2 &&
      afterUndo.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
      afterUndo.value().timeline.layers.size() == 1 &&
      afterRedo.value().project.revision == grapple::foundation::RevisionId{"rev_8"} &&
      afterRedo.value().timeline.layers.size() == 2;

    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Center the camera with editable controls.");
    window.clickStewardCreateCameraEffect();
    window.setEffectParamControlValue(grapple::runtime::builtin_effect::PositionXParam, 0.25);

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
    const std::string stewardAfterUndo = window.stewardContents();

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
    const std::string stewardAfterRedo = window.stewardContents();

    std::cout << "afterParamEditRevision=" << afterParamEdit.value().project.revision.value() << '\n';
    std::cout << "afterParamEditValue=" << std::get<double>(editedParam.value()->value) << '\n';
    std::cout << "afterParamEditCameraX=" << editedCameraX.value() << '\n';
    std::cout << "afterParamUndoRevision=" << afterParamUndo.value().project.revision.value() << '\n';
    std::cout << "afterParamUndoValue=" << std::get<double>(undoneParam.value()->value) << '\n';
    std::cout << "afterParamUndoCameraX=" << undoneCameraX.value() << '\n';
    std::cout << "afterParamRedoRevision=" << afterParamRedo.value().project.revision.value() << '\n';
    std::cout << "afterParamRedoValue=" << std::get<double>(redoneParam.value()->value) << '\n';
    std::cout << "afterParamRedoCameraX=" << redoneCameraX.value() << '\n';
    std::cout << "stewardAfterUndo=" << stewardAfterUndo << '\n';
    std::cout << "stewardAfterRedo=" << stewardAfterRedo << '\n';
    return trackUndoRedoOk &&
           std::holds_alternative<double>(editedParam.value()->value) &&
           std::holds_alternative<double>(undoneParam.value()->value) &&
           std::holds_alternative<double>(redoneParam.value()->value) &&
           approx(std::get<double>(editedParam.value()->value), 0.25) &&
           approx(editedCameraX.value(), 0.25) &&
           approx(std::get<double>(undoneParam.value()->value), 0.0) &&
           approx(undoneCameraX.value(), 0.0) &&
           !undoneParam.value()->lastEditedRevision.has_value() &&
           stewardAfterUndo.find("Position X=0 [-1..1 step 0.01] last changed by desktop") == std::string::npos &&
           approx(std::get<double>(redoneParam.value()->value), 0.25) &&
           approx(redoneCameraX.value(), 0.25) &&
           redoneParam.value()->lastEditedRevision.has_value() &&
           stewardAfterRedo.find("Position X=0.25 [-1..1 step 0.01] last changed by desktop at ") != std::string::npos
      ? 0
      : 1;
  }

  if (setEffectParamSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Shift the camera right with editable controls.");
    window.clickStewardCreateCameraEffect();
    window.setEffectParamControlValue(grapple::runtime::builtin_effect::PositionXParam, 0.25);
    window.setEffectParamControlValue(grapple::runtime::builtin_effect::ZoomParam, 1.5);
    const std::string inspector = window.inspectorContents();
    const std::string log = window.logContents();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           inspector.find("Position X (position_x)=0.25") != std::string::npos &&
           inspector.find("Zoom (zoom)=1.5") != std::string::npos &&
           log.find("Updated effect parameter") == std::string::npos
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
           inspector.find("No effects attached.") != std::string::npos
      ? 0
      : 1;
  }

  if (effectKeyframeSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Animate camera position with editable controls.");
    window.clickStewardCreateCameraEffect();
    window.seekTo(grapple::foundation::TimeSeconds{2.0});
    window.setEffectParamControlValue(grapple::runtime::builtin_effect::PositionXParam, 0.25);
    window.setEffectParamKeyframeAtPlayhead(grapple::runtime::builtin_effect::PositionXParam);
    const auto afterKeyframe = workspace.value().project().buildViewModel();
    if (!afterKeyframe) {
      printError(afterKeyframe.error());
      return 1;
    }
    window.setEffectParamControlValue(grapple::runtime::builtin_effect::PositionXParam, 0.5);
    window.setEffectParamKeyframeAtPlayhead(grapple::runtime::builtin_effect::PositionXParam);
    const auto afterKeyframeUpdate = workspace.value().project().buildViewModel();
    if (!afterKeyframeUpdate) {
      printError(afterKeyframeUpdate.error());
      return 1;
    }
    const std::string buttonAtKeyframe = window.effectParamKeyframeButtonText(grapple::runtime::builtin_effect::PositionXParam);
    window.seekTo(grapple::foundation::TimeSeconds{0.0});
    const std::string buttonAwayFromKeyframe = window.effectParamKeyframeButtonText(grapple::runtime::builtin_effect::PositionXParam);
    window.seekTo(grapple::foundation::TimeSeconds{2.0});
    const std::string buttonBackAtKeyframe = window.effectParamKeyframeButtonText(grapple::runtime::builtin_effect::PositionXParam);
    const std::string effectParamPanelAfterUpdate = window.effectParamPanelText();
    const std::string stewardAfterKeyframeUpdate = window.stewardContents();
    window.deleteEffectParamKeyframeControl(grapple::runtime::builtin_effect::PositionXParam, 0);
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
    std::cout << "buttonAtKeyframe=" << buttonAtKeyframe << '\n';
    std::cout << "buttonAwayFromKeyframe=" << buttonAwayFromKeyframe << '\n';
    std::cout << "buttonBackAtKeyframe=" << buttonBackAtKeyframe << '\n';
    std::cout << "effectParamPanelAfterUpdate=" << effectParamPanelAfterUpdate << '\n';
    std::cout << "stewardAfterKeyframeUpdate=" << stewardAfterKeyframeUpdate << '\n';
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
           buttonAtKeyframe == "Update" &&
           buttonAwayFromKeyframe == "Set" &&
           buttonBackAtKeyframe == "Update" &&
           effectParamPanelAfterUpdate.find("2s = 0.5 last changed by desktop at ") != std::string::npos &&
           stewardAfterKeyframeUpdate.find("keyframe 2s=0.5 last changed by desktop at ") != std::string::npos &&
           keyframesAfterDelete.empty()
      ? 0
      : 1;
  }

  if (addEffectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    const auto selectedClipBeforeCreate = window.selectedNodeId();
    window.setStewardIntent("Center the walking subject with exposed controls.");
    window.clickStewardCreateCameraEffect();
    const auto selectedAfterCreate = window.selectedNodeId();
    window.clickFirstTimelineClip();
    const auto selectedClipBeforeShowControls = window.selectedNodeId();
    window.clickStewardCreateCameraEffect();
    const auto selectedAfterShowControls = window.selectedNodeId();
    const std::string inspector = window.inspectorContents();
    const std::string logText = window.logContents();
    const std::string steward = window.stewardContents();
    const std::string stewardActionText = window.stewardPrimaryActionText();
    const bool stewardActionEnabled = window.stewardPrimaryActionEnabled();
    const std::string effectParamTitle = window.effectParamTitleText();
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
    std::cout << "stewardAction=" << stewardActionText << '\n';
    std::cout << "stewardActionEnabled=" << (stewardActionEnabled ? "true" : "false") << '\n';
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
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
           steward.find("Position X=0 [-1..1 step 0.01]") != std::string::npos &&
           steward.find("Position Y=0 [-1..1 step 0.01]") != std::string::npos &&
           steward.find("Zoom=1.1 [0.25..4 step 0.01]") != std::string::npos &&
           steward.find("Applied edits") != std::string::npos &&
           steward.find("- " + createdRevisionText + " Camera Transform on Camera: Center the walking subject with exposed controls.") != std::string::npos &&
           steward.find("Recent Steward runs") != std::string::npos &&
           steward.find("- Center the walking subject with exposed controls. [succeeded]") != std::string::npos &&
           steward.find("effect.create_node -> succeeded at " + createdRevisionText) != std::string::npos &&
           steward.find("effect.create_node -> failed") == std::string::npos &&
           steward.find("- Center the walking subject with exposed controls.") != std::string::npos &&
           selectedClipBeforeCreate.has_value() &&
           selectedClipBeforeCreate.value() == expectedClipNodeId &&
           selectedAfterCreate.has_value() &&
           selectedAfterCreate.value() == expectedCameraNodeId &&
           selectedClipBeforeShowControls.has_value() &&
           selectedClipBeforeShowControls.value() == expectedClipNodeId &&
           selectedAfterShowControls.has_value() &&
           selectedAfterShowControls.value() == expectedCameraNodeId &&
           stewardActionText == "Editable Controls Shown" &&
           !stewardActionEnabled &&
           effectParamTitle == "Camera Transform on Camera" &&
           logText.find("Preview refreshed") == std::string::npos &&
           logText.find("steward.camera_transform_exists") == std::string::npos &&
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

  if (playbackSmoke) {
    window.startPlayback();
    window.advancePlaybackFrame();
    window.pausePlayback();
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead.value > 0.0 ? 0 : 1;
  }

  if (openPackageSmoke) {
    const auto write = workspace.value().writePackage();
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

  if (exportSettingsSmoke) {
    window.show();
    app.processEvents();
    const std::filesystem::path outputPath{"/tmp/grapple-desktop-export-settings.avi"};
    std::filesystem::remove(outputPath);
    window.setExportResolutionControlValue(320, 180);
    window.setExportFrameRateControlValue(10.0);
    window.setExportCodecControlValue("mjpeg");
    window.exportVideoFile(grapple::foundation::FilePath{outputPath.string()});
    const std::string log = window.logContents();
    const bool exists = std::filesystem::exists(outputPath);
    const auto size = exists ? std::filesystem::file_size(outputPath) : 0U;
    std::cout << "exists=" << (exists ? "true" : "false") << '\n';
    std::cout << "size=" << size << '\n';
    std::cout << "log=" << log << '\n';
    return exists &&
           size > 0U &&
           log.find("Export progress 100%") != std::string::npos &&
           log.find("Export evaluated 100 frames") != std::string::npos
      ? 0
      : 1;
  }

  if (productLoopSmoke) {
    window.show();
    app.processEvents();
    const std::filesystem::path outputPath{"/tmp/grapple-desktop-product-loop.avi"};
    std::filesystem::remove(outputPath);

    window.importMediaFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/starter-gradient.avi"});
    window.addSelectedMediaToTimeline();
    window.setStewardIntent("Center the subject with editable camera controls.");
    window.clickStewardCreateCameraEffect();
    window.setSelectedTargetNumericEffectParam(grapple::runtime::builtin_effect::PositionXParam, 0.25);
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
      tunedPreviewFrame.value().frame.cameras.front().state.transform.position.x == 0.25;
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
    const std::string inspector = window.inspectorContents();
    const std::string stewardActionText = window.stewardPrimaryActionText();
    const bool stewardActionEnabled = window.stewardPrimaryActionEnabled();
    const std::string effectParamTitle = window.effectParamTitleText();
    const std::string effectParamPanel = window.effectParamPanelText();
    const std::string log = window.logContents();
    const bool exists = std::filesystem::exists(outputPath);
    const auto size = exists ? std::filesystem::file_size(outputPath) : 0U;
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
            const auto param = std::find_if(
              effect.params.begin(),
              effect.params.end(),
              [](const grapple::app::AppEffectParamRow& row) {
                return row.name == grapple::runtime::builtin_effect::PositionXParam;
              }
            );
            return param != effect.params.end() &&
                   std::holds_alternative<double>(param->value) &&
                   std::get<double>(param->value) == 0.25;
          }
        );
      }
    );

    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    std::cout << "effects=" << viewModel.value().timeline.effectCount << '\n';
    std::cout << "evaluatedTunedPreview=" << (hasEvaluatedTunedPreview ? "true" : "false") << '\n';
    std::cout << "exists=" << (exists ? "true" : "false") << '\n';
    std::cout << "size=" << size << '\n';
    std::cout << "inspector=" << inspector << '\n';
    std::cout << "steward=" << steward << '\n';
    std::cout << "stewardAction=" << stewardActionText << '\n';
    std::cout << "stewardActionEnabled=" << (stewardActionEnabled ? "true" : "false") << '\n';
    std::cout << "effectParamTitle=" << effectParamTitle << '\n';
    std::cout << "effectParamPanel=" << effectParamPanel << '\n';
    std::cout << "log=" << log << '\n';
    return viewModel.value().assets.count == 1 &&
           viewModel.value().timeline.clips.size() == 1 &&
           viewModel.value().timeline.cameras.size() == 1 &&
           viewModel.value().timeline.effectCount == 1 &&
           hasTunedEditableEffect &&
           hasEvaluatedTunedPreview &&
           steward.find("1 clips | 1 cameras | 1 editable effects") != std::string::npos &&
           steward.find("Position X=0.25 [-1..1 step 0.01]") != std::string::npos &&
           steward.find("Position X=0.25 [-1..1 step 0.01] last changed by desktop at ") != std::string::npos &&
           steward.find("Applied edits") != std::string::npos &&
           steward.find("Camera Transform on Camera: Center the subject with editable camera controls.") != std::string::npos &&
           stewardActionText == "Editable Controls Shown" &&
           !stewardActionEnabled &&
           effectParamTitle == "Camera Transform on Camera" &&
           inspector.find("Position X (position_x)=0.25") != std::string::npos &&
           inspector.find("last changed by desktop at ") != std::string::npos &&
           effectParamPanel.find("Last changed by desktop at ") != std::string::npos &&
           log.find("Imported starter-gradient") != std::string::npos &&
           log.find("Added starter-gradient to timeline") != std::string::npos &&
           log.find("Steward applied camera edit") != std::string::npos &&
           log.find(expectedExportProvenance) != std::string::npos &&
           exists &&
           size > 0U
      ? 0
      : 1;
  }

  if (editSaveSmoke) {
    window.importMediaFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/starter-gradient.avi"});
    window.addSelectedMediaToTimeline();
    window.clickFirstTimelineCamera();
    window.setStewardIntent("Persist editable camera controls.");
    window.clickStewardCreateCameraEffect();
    window.setSelectedTargetNumericEffectParam(grapple::runtime::builtin_effect::PositionXParam, 0.25);
    const auto write = workspace.value().writePackage();
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
                return row.name == grapple::runtime::builtin_effect::PositionXParam;
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
    const std::filesystem::path reopenedExportPath{"/tmp/grapple-desktop-reopened-export.avi"};
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
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_9"} &&
           viewModel.value().assets.count == 2 &&
           viewModel.value().timeline.clips.size() == 2 &&
           viewModel.value().timeline.effectCount == 1 &&
           reopened.value().project().packageState().commandLog.records().size() == 9 &&
           reopenedTunedEffect &&
           reopenedPreviewTuned &&
           stewardContextRestored &&
           reopenedExportMatchesPlan
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
    window.clickStewardCreateCameraEffect();
    window.setSelectedTargetNumericEffectParam(grapple::runtime::builtin_effect::PositionXParam, 0.25);
    window.setSelectedTargetNumericEffectParam(grapple::runtime::builtin_effect::ZoomParam, 1.5);
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
