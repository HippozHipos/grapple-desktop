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

#include <algorithm>
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
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else if (argument == "--effect-screenshot" && index + 1 < argc) {
      effectScreenshotPath = argv[++index];
    } else {
      std::cerr << "Expected --smoke, --mutate-smoke, --seek-smoke, --timeline-seek-smoke, --select-smoke, --select-audio-clip-smoke, --select-audio-track-smoke, --select-camera-smoke, --select-second-camera-smoke, --steward-smoke, --import-smoke, --import-media-types-smoke, --add-video-smoke, --empty-add-video-smoke, --empty-add-track-smoke, --empty-add-camera-smoke, --update-camera-smoke, --add-note-smoke, --update-note-smoke, --move-clip-smoke, --trim-clip-smoke, --nudge-clip-smoke, --undo-redo-smoke, --add-effect-smoke, --set-effect-param-smoke, --effect-keyframe-smoke, --delete-effect-smoke, --delete-smoke, --delete-track-smoke, --playback-smoke, --open-package-smoke, --edit-save-smoke, --export-settings-smoke, --screenshot <path>, or --effect-screenshot <path>.\n";
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
  const auto demoVideo = grapple::demo::ensureStarterDemoVideo();
  if (!demoVideo) {
    printError(demoVideo.error());
    return 1;
  }

  if (!emptyAddVideoSmoke && !emptyAddTrackSmoke && !emptyAddCameraSmoke && !importMediaTypesSmoke) {
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
    return selectedNodeId.value() == grapple::foundation::NodeId{"node_clip_3"} &&
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
    const auto selectedNodeId = window.selectedNodeId();
    std::cout << "steward=" << steward << '\n';
    if (selectedNodeId.has_value()) {
      std::cout << "selected=" << selectedNodeId->value() << '\n';
    }
    return steward.find("Current request") != std::string::npos &&
           steward.find("Center the subject with an editable camera transform.") != std::string::npos &&
           steward.find("Editable controls") != std::string::npos &&
           steward.find("- no editable controls yet") != std::string::npos &&
           steward.find("Applied edits") != std::string::npos &&
           steward.find("- no applied edits yet") != std::string::npos &&
           selectedNodeId.has_value() &&
           selectedNodeId.value() == grapple::foundation::NodeId{"node_camera_4"}
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
    window.addCamera();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "compositions=" << viewModel.value().timeline.compositions.size() << '\n';
    std::cout << "cameras=" << viewModel.value().timeline.cameras.size() << '\n';
    if (window.selectedNodeId().has_value()) {
      std::cout << "selectedNode=" << window.selectedNodeId()->value() << '\n';
    }
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_2"} &&
           viewModel.value().timeline.compositions.size() == 1 &&
           viewModel.value().timeline.cameras.size() == 1 &&
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
    return afterAdd.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           afterAdd.value().timeline.layers.size() == 2 &&
           afterUndo.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           afterUndo.value().timeline.layers.size() == 1 &&
           afterRedo.value().project.revision == grapple::foundation::RevisionId{"rev_8"} &&
           afterRedo.value().timeline.layers.size() == 2
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
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "inspector=" << inspector << '\n';
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_7"} &&
           inspector.find("Position X (position_x)=0.25") != std::string::npos &&
           inspector.find("Zoom (zoom)=1.5") != std::string::npos
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
    std::cout << "afterDeleteRevision=" << afterKeyframeDelete.value().project.revision.value() << '\n';
    std::cout << "afterDeleteKeyframes=" << keyframesAfterDelete.size() << '\n';
    return keyframesAfterSet.size() == 1 &&
           keyframesAfterSet[0].time == grapple::foundation::TimeSeconds{2.0} &&
           std::get<double>(keyframesAfterSet[0].value) == 0.25 &&
           keyframesAfterUpdate.size() == 1 &&
           keyframesAfterUpdate[0].keyframeId == keyframesAfterSet[0].keyframeId &&
           std::get<double>(keyframesAfterUpdate[0].value) == 0.5 &&
           keyframesAfterDelete.empty()
      ? 0
      : 1;
  }

  if (addEffectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
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
               graph.effects.front().implementationKind == "builtin" &&
               graph.effects.front().cameraTransformEffect;
      }
    );
    return viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           cameraHasEffect &&
           inspector.find("Camera Transform") != std::string::npos &&
           inspector.find("Position X (position_x)=0") != std::string::npos &&
           intentRecorded &&
           steward.find("Position X=0 [-1..1 step 0.01]") != std::string::npos &&
           steward.find("Position Y=0 [-1..1 step 0.01]") != std::string::npos &&
           steward.find("Zoom=1.1 [0.25..4 step 0.01]") != std::string::npos &&
           steward.find("Recent Steward runs") != std::string::npos &&
           steward.find("project edit -> succeeded") != std::string::npos &&
           steward.find("project edit -> failed") == std::string::npos &&
           steward.find("- Center the walking subject with exposed controls.") != std::string::npos &&
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

  if (editSaveSmoke) {
    window.importMediaFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/starter-gradient.avi"});
    window.addSelectedMediaToTimeline();
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
