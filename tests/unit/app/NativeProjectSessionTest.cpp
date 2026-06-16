#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativeEffectSession.hpp>
#include <grapple/app/NativeMediaImport.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeStewardPlanner.hpp>
#include <grapple/app/NativeStewardSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/history/HistorySerializer.hpp>
#include <grapple/jobs/JobQueue.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/render/LocalRenderCore.hpp>
#include <grapple/render/LocalRenderSystem.hpp>
#include <grapple/effects/BuiltinEffects.hpp>
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
#include <thread>
#include <variant>
#include <vector>

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

class RecordingProgressSink final : public grapple::jobs::IProgressSink {
public:
  void reportProgress(double progress) override {
    records.push_back(progress);
  }

  std::vector<double> records;
};

class CapturingRenderRangeSink final : public grapple::render::IRenderRangeSink {
public:
  grapple::foundation::Result<void> writeFrame(
    std::size_t frameIndex,
    const grapple::render::RenderFrameResult& frame
  ) override {
    frameIndexes.push_back(frameIndex);
    frameTimes.push_back(frame.frame.time);
    frameCameras.push_back(frame.frame.cameras);
    frameImages.push_back(frame.frame.image);
    return {};
  }

  std::vector<std::size_t> frameIndexes;
  std::vector<grapple::foundation::TimeSeconds> frameTimes;
  std::vector<std::vector<grapple::render::RenderedCamera>> frameCameras;
  std::vector<std::optional<grapple::render::RenderedImage>> frameImages;
};

class CancellingProgressSink final : public grapple::jobs::IProgressSink {
public:
  explicit CancellingProgressSink(grapple::jobs::CancellationToken& cancellation)
    : cancellation_{cancellation} {}

  void reportProgress(double progress) override {
    records.push_back(progress);
    cancellation_.cancel();
  }

  grapple::jobs::CancellationToken& cancellation_;
  std::vector<double> records;
};

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
           node.payload.implementation.entrypoint == grapple::effects::builtin_effect::CameraTransformEntrypoint;
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

  app::NativeStewardPlanner stewardPlanner;
  const app::CameraTransformIntentDefaults zoomRightDefaults =
    stewardPlanner.cameraTransformDefaultsForIntent("Move right and zoom in on the subject");
  GRAPPLE_REQUIRE(zoomRightDefaults.positionX == 0.25);
  GRAPPLE_REQUIRE(zoomRightDefaults.positionY == 0.0);
  GRAPPLE_REQUIRE(zoomRightDefaults.zoom == 1.5);
  const app::CameraTransformIntentDefaults slightZoomRightDefaults =
    stewardPlanner.cameraTransformDefaultsForIntent("Slightly move right and zoom in a little");
  GRAPPLE_REQUIRE(slightZoomRightDefaults.positionX == 0.125);
  GRAPPLE_REQUIRE(slightZoomRightDefaults.positionY == 0.0);
  GRAPPLE_REQUIRE(slightZoomRightDefaults.zoom == 1.25);
  const std::optional<app::CameraTransformMotionKeyframes> panLeftMotion =
    stewardPlanner.cameraMotionKeyframesForIntent(
      "slowly pan left",
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}}
    );
  GRAPPLE_REQUIRE(panLeftMotion.has_value());
  GRAPPLE_REQUIRE(panLeftMotion->paramName == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(panLeftMotion->startValue == 0.0);
  GRAPPLE_REQUIRE(panLeftMotion->endValue == -0.25);
  GRAPPLE_REQUIRE(panLeftMotion->endTime == foundation::TimeSeconds{4.0});
  const std::optional<app::CameraTransformMotionKeyframes> farPanRightMotion =
    stewardPlanner.cameraMotionKeyframesForIntent(
      "slowly pan far right",
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}}
  );
  GRAPPLE_REQUIRE(farPanRightMotion.has_value());
  GRAPPLE_REQUIRE(farPanRightMotion->paramName == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(farPanRightMotion->startValue == 0.0);
  GRAPPLE_REQUIRE(farPanRightMotion->endValue == 0.5);
  GRAPPLE_REQUIRE(farPanRightMotion->endTime == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(stewardPlanner.cameraTransformDeleteIntentTargetsCameraControls("remove camera controls"));
  GRAPPLE_REQUIRE(stewardPlanner.cameraTransformDeleteIntentTargetsCameraControls("delete framing effect"));
  GRAPPLE_REQUIRE(!stewardPlanner.cameraTransformDeleteIntentTargetsCameraControls("reset camera"));
  GRAPPLE_REQUIRE(!stewardPlanner.cameraTransformDeleteIntentTargetsCameraControls("move camera right"));
  GRAPPLE_REQUIRE(stewardPlanner.cameraUpdateIntentTargetsCamera("rename camera to \"Closeup\""));
  GRAPPLE_REQUIRE(stewardPlanner.cameraUpdateIntentTargetsCamera("set camera focal length to 50"));
  GRAPPLE_REQUIRE(!stewardPlanner.cameraUpdateIntentTargetsCamera("center the camera"));
  timeline::CameraPayload plannedCamera{
    "Camera",
    timeline::CameraState{timeline::Transform2D{}, timeline::CameraLens{35.0}}
  };
  const auto plannedCameraRename = stewardPlanner.cameraUpdateForIntent(
    plannedCamera,
    "Rename camera to \"Closeup\"."
  );
  GRAPPLE_REQUIRE(plannedCameraRename);
  GRAPPLE_REQUIRE(plannedCameraRename.value().payload.name == "Closeup");
  GRAPPLE_REQUIRE(plannedCameraRename.value().payload.state.lens.focalLength == 35.0);
  const auto plannedCameraLens = stewardPlanner.cameraUpdateForIntent(
    plannedCamera,
    "Set camera focal length to 50."
  );
  GRAPPLE_REQUIRE(plannedCameraLens);
  GRAPPLE_REQUIRE(plannedCameraLens.value().payload.name == "Camera");
  GRAPPLE_REQUIRE(plannedCameraLens.value().payload.state.lens.focalLength == 50.0);
  const auto plannedCameraNumberedNameAndLens = stewardPlanner.cameraUpdateForIntent(
    plannedCamera,
    "Rename camera to \"Camera 2\" and set camera focal length to 50."
  );
  GRAPPLE_REQUIRE(plannedCameraNumberedNameAndLens);
  GRAPPLE_REQUIRE(plannedCameraNumberedNameAndLens.value().payload.name == "Camera 2");
  GRAPPLE_REQUIRE(plannedCameraNumberedNameAndLens.value().payload.state.lens.focalLength == 50.0);
  GRAPPLE_REQUIRE(stewardPlanner.undoIntentTargetsLastEdit("undo last edit"));
  GRAPPLE_REQUIRE(stewardPlanner.undoIntentTargetsLastEdit("revert the previous change"));
  GRAPPLE_REQUIRE(!stewardPlanner.undoIntentTargetsLastEdit("remove selected clip"));
  GRAPPLE_REQUIRE(!stewardPlanner.undoIntentTargetsLastEdit("reset camera"));
  GRAPPLE_REQUIRE(stewardPlanner.redoIntentTargetsLastUndoneEdit("redo last edit"));
  GRAPPLE_REQUIRE(!stewardPlanner.redoIntentTargetsLastUndoneEdit("undo last edit"));
  GRAPPLE_REQUIRE(stewardPlanner.historyIntentTargetsEdit("undo last edit"));
  GRAPPLE_REQUIRE(stewardPlanner.historyIntentTargetsEdit("redo last edit"));
  GRAPPLE_REQUIRE(!stewardPlanner.historyIntentTargetsEdit("delete selected clip"));
  timeline::Transform2D plannedClipInputTransform;
  const timeline::ClipPayload plannedClipInput{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{6.0}},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{6.0}},
    1.0,
    foundation::AssetId{"asset_planned_clip"},
    plannedClipInputTransform
  };
  const auto plannedClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "move right and make smaller");
  GRAPPLE_REQUIRE(plannedClipEdit);
  GRAPPLE_REQUIRE(plannedClipEdit.value().transformChanged);
  GRAPPLE_REQUIRE(!plannedClipEdit.value().playbackRateChanged);
  GRAPPLE_REQUIRE(plannedClipEdit.value().transform.position.x == 0.25);
  GRAPPLE_REQUIRE(plannedClipEdit.value().transform.position.y == 0.0);
  GRAPPLE_REQUIRE(plannedClipEdit.value().transform.scale.x == 0.75);
  GRAPPLE_REQUIRE(plannedClipEdit.value().transform.scale.y == 0.75);
  const auto twoAxisClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "move right and up");
  GRAPPLE_REQUIRE(twoAxisClipEdit);
  GRAPPLE_REQUIRE(twoAxisClipEdit.value().transform.position.x == 0.25);
  GRAPPLE_REQUIRE(twoAxisClipEdit.value().transform.position.y == -0.2);
  const auto strongClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "move far right and make much bigger");
  GRAPPLE_REQUIRE(strongClipEdit);
  GRAPPLE_REQUIRE(strongClipEdit.value().transform.position.x == 0.5);
  GRAPPLE_REQUIRE(strongClipEdit.value().transform.scale.x == 1.5);
  GRAPPLE_REQUIRE(strongClipEdit.value().transform.scale.y == 1.5);
  const auto rotateClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "rotate slightly left and make invisible");
  GRAPPLE_REQUIRE(rotateClipEdit);
  GRAPPLE_REQUIRE(rotateClipEdit.value().transform.position.x == 0.0);
  GRAPPLE_REQUIRE(rotateClipEdit.value().transform.position.y == 0.0);
  GRAPPLE_REQUIRE(rotateClipEdit.value().transform.scale.x == 1.0);
  GRAPPLE_REQUIRE(rotateClipEdit.value().transform.scale.y == 1.0);
  GRAPPLE_REQUIRE(rotateClipEdit.value().transform.rotationDegrees == -7.5);
  GRAPPLE_REQUIRE(rotateClipEdit.value().transform.opacity == 0.0);
  const auto rotateRightClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "rotate right");
  GRAPPLE_REQUIRE(rotateRightClipEdit);
  GRAPPLE_REQUIRE(rotateRightClipEdit.value().transform.position.x == 0.0);
  GRAPPLE_REQUIRE(rotateRightClipEdit.value().transform.rotationDegrees == 15.0);
  const auto rotateWithoutCommaClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "move right and rotate left");
  GRAPPLE_REQUIRE(rotateWithoutCommaClipEdit);
  GRAPPLE_REQUIRE(rotateWithoutCommaClipEdit.value().transform.position.x == 0.25);
  GRAPPLE_REQUIRE(rotateWithoutCommaClipEdit.value().transform.rotationDegrees == -15.0);
  GRAPPLE_REQUIRE(stewardPlanner.clipEditIntentTargetsClip("rotate selected clip slightly left"));
  GRAPPLE_REQUIRE(stewardPlanner.clipEditIntentTargetsClip("make video invisible"));
  GRAPPLE_REQUIRE(stewardPlanner.clipEditIntentTargetsClip("speed up selected clip"));
  GRAPPLE_REQUIRE(stewardPlanner.clipEditIntentTargetsClip("move selected clip later"));
  GRAPPLE_REQUIRE(stewardPlanner.clipEditIntentTargetsClip("shorten selected clip"));
  GRAPPLE_REQUIRE(!stewardPlanner.clipEditIntentTargetsClip("slowly pan right"));
  GRAPPLE_REQUIRE(!stewardPlanner.clipEditIntentTargetsClip("make clip cinematic"));
  GRAPPLE_REQUIRE(stewardPlanner.clipDeleteIntentTargetsClip("delete selected clip"));
  GRAPPLE_REQUIRE(stewardPlanner.clipDeleteIntentTargetsClip("remove selected title"));
  GRAPPLE_REQUIRE(!stewardPlanner.clipEditIntentTargetsClip("delete selected clip"));
  GRAPPLE_REQUIRE(!stewardPlanner.clipDeleteIntentTargetsClip("make clip smaller"));
  GRAPPLE_REQUIRE(stewardPlanner.trackCreateIntentTargetsTrack("add video track"));
  GRAPPLE_REQUIRE(stewardPlanner.trackCreateIntentTargetsTrack("create audio layer"));
  GRAPPLE_REQUIRE(!stewardPlanner.trackCreateIntentTargetsTrack("delete selected track"));
  GRAPPLE_REQUIRE(!stewardPlanner.trackCreateIntentTargetsTrack("add title"));
  GRAPPLE_REQUIRE(stewardPlanner.trackDefaultsForIntent("add video track").kind == timeline::TrackKind::Visual);
  GRAPPLE_REQUIRE(stewardPlanner.trackDefaultsForIntent("create audio layer").kind == timeline::TrackKind::Audio);
  GRAPPLE_REQUIRE(stewardPlanner.trackDeleteIntentTargetsTrack("delete selected track"));
  GRAPPLE_REQUIRE(stewardPlanner.trackDeleteIntentTargetsTrack("remove selected layer"));
  GRAPPLE_REQUIRE(!stewardPlanner.trackDeleteIntentTargetsTrack("delete selected clip"));
  GRAPPLE_REQUIRE(!stewardPlanner.trackDeleteIntentTargetsTrack("make track louder"));
  const auto mixedClipEdit =
    stewardPlanner.clipEditForIntent(
      plannedClipInput,
      "Move selected clip right, rotate slightly left, make it smaller, and make it invisible."
    );
  GRAPPLE_REQUIRE(mixedClipEdit);
  GRAPPLE_REQUIRE(mixedClipEdit.value().transform.position.x == 0.25);
  GRAPPLE_REQUIRE(mixedClipEdit.value().transform.scale.x == 0.75);
  GRAPPLE_REQUIRE(mixedClipEdit.value().transform.scale.y == 0.75);
  GRAPPLE_REQUIRE(mixedClipEdit.value().transform.rotationDegrees == -7.5);
  GRAPPLE_REQUIRE(mixedClipEdit.value().transform.opacity == 0.0);
  timeline::Transform2D rotatedClipInputTransform;
  rotatedClipInputTransform.rotationDegrees = 22.0;
  rotatedClipInputTransform.opacity = 0.5;
  const timeline::ClipPayload rotatedClipInput{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{6.0}},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{6.0}},
    1.0,
    foundation::AssetId{"asset_rotated_clip"},
    rotatedClipInputTransform
  };
  const auto straightenedClipEdit =
    stewardPlanner.clipEditForIntent(rotatedClipInput, "straighten and make opaque");
  GRAPPLE_REQUIRE(straightenedClipEdit);
  GRAPPLE_REQUIRE(straightenedClipEdit.value().transform.rotationDegrees == 0.0);
  GRAPPLE_REQUIRE(straightenedClipEdit.value().transform.opacity == 1.0);
  const auto fasterClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "make selected clip faster");
  GRAPPLE_REQUIRE(fasterClipEdit);
  GRAPPLE_REQUIRE(!fasterClipEdit.value().transformChanged);
  GRAPPLE_REQUIRE(fasterClipEdit.value().playbackRateChanged);
  GRAPPLE_REQUIRE(fasterClipEdit.value().transform == plannedClipInputTransform);
  GRAPPLE_REQUIRE(fasterClipEdit.value().playbackRate == 1.25);
  const auto halfSpeedClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "set clip to half speed");
  GRAPPLE_REQUIRE(halfSpeedClipEdit);
  GRAPPLE_REQUIRE(!halfSpeedClipEdit.value().transformChanged);
  GRAPPLE_REQUIRE(halfSpeedClipEdit.value().playbackRateChanged);
  GRAPPLE_REQUIRE(halfSpeedClipEdit.value().playbackRate == 0.5);
  const auto laterClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "move selected clip later");
  GRAPPLE_REQUIRE(laterClipEdit);
  GRAPPLE_REQUIRE(laterClipEdit.value().moveChanged);
  GRAPPLE_REQUIRE(!laterClipEdit.value().trimChanged);
  GRAPPLE_REQUIRE(!laterClipEdit.value().transformChanged);
  GRAPPLE_REQUIRE(laterClipEdit.value().newStart == foundation::TimeSeconds{1.0});
  const timeline::ClipPayload laterStartingClipInput{
    timeline::ClipKind::Video,
    foundation::TimeRange{foundation::TimeSeconds{2.0}, foundation::TimeSeconds{8.0}},
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{6.0}},
    1.0,
    foundation::AssetId{"asset_later_starting_clip"},
    plannedClipInputTransform
  };
  const auto earlierClipEdit =
    stewardPlanner.clipEditForIntent(laterStartingClipInput, "move selected clip earlier");
  GRAPPLE_REQUIRE(earlierClipEdit);
  GRAPPLE_REQUIRE(earlierClipEdit.value().moveChanged);
  GRAPPLE_REQUIRE(earlierClipEdit.value().newStart == foundation::TimeSeconds{1.0});
  const auto shorterClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "shorten selected clip");
  GRAPPLE_REQUIRE(shorterClipEdit);
  GRAPPLE_REQUIRE(!shorterClipEdit.value().moveChanged);
  GRAPPLE_REQUIRE(shorterClipEdit.value().trimChanged);
  GRAPPLE_REQUIRE((shorterClipEdit.value().timelineRange == foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}}));
  GRAPPLE_REQUIRE((shorterClipEdit.value().sourceRange == foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{5.0}}));
  const auto unknownClipEdit =
    stewardPlanner.clipEditForIntent(plannedClipInput, "make it cinematic");
  GRAPPLE_REQUIRE(!unknownClipEdit);
  GRAPPLE_REQUIRE(unknownClipEdit.error().code == "steward.clip_edit_intent_unknown");
  GRAPPLE_REQUIRE(stewardPlanner.textClipIntentTargetsText("add title \"Opening Title\""));
  GRAPPLE_REQUIRE(stewardPlanner.textClipIntentTargetsText("create lower third saying Jane Doe"));
  GRAPPLE_REQUIRE(!stewardPlanner.textClipIntentTargetsText("slowly pan right"));
  const app::TextClipIntentDefaults titleTextDefaults =
    stewardPlanner.textClipDefaultsForIntent("add title \"Opening Title\"");
  GRAPPLE_REQUIRE(titleTextDefaults.text == "Opening Title");
  GRAPPLE_REQUIRE(titleTextDefaults.duration == foundation::TimeSeconds{3.0});
  GRAPPLE_REQUIRE(titleTextDefaults.transform.position.y == 0.35);
  GRAPPLE_REQUIRE(titleTextDefaults.style.fontSize == 64.0);
  const app::TextClipIntentDefaults lowerThirdDefaults =
    stewardPlanner.textClipDefaultsForIntent("create lower third saying Jane Doe");
  GRAPPLE_REQUIRE(lowerThirdDefaults.text == "Jane Doe");
  GRAPPLE_REQUIRE(lowerThirdDefaults.transform.position.y == -0.35);
  GRAPPLE_REQUIRE(lowerThirdDefaults.style.fontSize == 44.0);
  const timeline::TextClipPayload plannedTextClip{
    "Opening Title",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}},
    timeline::Transform2D{},
    timeline::TextClipStyle{64.0, foundation::Vec3{1.0, 1.0, 1.0}}
  };
  GRAPPLE_REQUIRE(stewardPlanner.textClipEditIntentTargetsTextClip("Change title to \"Final Title\" and make font smaller"));
  const auto textClipEdit =
    stewardPlanner.textClipEditForIntent(plannedTextClip, "Change title to \"Final Title\" and make font smaller");
  GRAPPLE_REQUIRE(textClipEdit);
  GRAPPLE_REQUIRE(textClipEdit.value().payload.text == "Final Title");
  GRAPPLE_REQUIRE(textClipEdit.value().payload.style.fontSize == 48.0);
  const auto textClipPlacementEdit =
    stewardPlanner.textClipEditForIntent(
      plannedTextClip,
      "Move text right and up, make it shorter, and fade it."
    );
  GRAPPLE_REQUIRE(textClipPlacementEdit);
  GRAPPLE_REQUIRE(textClipPlacementEdit.value().payload.transform.position.x == 0.25);
  GRAPPLE_REQUIRE(textClipPlacementEdit.value().payload.transform.position.y == 0.2);
  GRAPPLE_REQUIRE(textClipPlacementEdit.value().payload.timelineRange.end == foundation::TimeSeconds{2.0});
  GRAPPLE_REQUIRE(textClipPlacementEdit.value().payload.transform.opacity == 0.5);
  GRAPPLE_REQUIRE(stewardPlanner.noteIntentTargetsNote("Add note \"Camera rationale\" saying Keep zoom editable."));
  GRAPPLE_REQUIRE(!stewardPlanner.noteIntentTargetsNote("slowly pan right"));
  const app::NoteIntentDefaults noteDefaults =
    stewardPlanner.noteDefaultsForIntent("Add note \"Camera rationale\" saying Keep zoom editable.");
  GRAPPLE_REQUIRE(noteDefaults.title == "Camera rationale");
  GRAPPLE_REQUIRE(noteDefaults.markdown == "Keep zoom editable");
  const timeline::NotePayload plannedNote{"Camera rationale", "Keep the offset editable."};
  GRAPPLE_REQUIRE(stewardPlanner.noteEditIntentTargetsNote("Update note to \"Keep camera controls editable.\""));
  const auto noteBodyEdit =
    stewardPlanner.noteEditForIntent(plannedNote, "Update note to \"Keep camera controls editable.\"");
  GRAPPLE_REQUIRE(noteBodyEdit);
  GRAPPLE_REQUIRE(noteBodyEdit.value().payload.title == "Camera rationale");
  GRAPPLE_REQUIRE(noteBodyEdit.value().payload.markdown == "Keep camera controls editable.");
  const auto noteTitleEdit =
    stewardPlanner.noteEditForIntent(plannedNote, "Rename note to \"Camera Notes\"");
  GRAPPLE_REQUIRE(noteTitleEdit);
  GRAPPLE_REQUIRE(noteTitleEdit.value().payload.title == "Camera Notes");
  GRAPPLE_REQUIRE(noteTitleEdit.value().payload.markdown == "Keep the offset editable.");

  const std::filesystem::path appPackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_primary_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  app::NativeProjectSession session{
    foundation::ProjectId{"proj_app"},
    "App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app"},
      foundation::FilePath{appPackageRoot.string()},
      storage::CurrentProjectPackageSchemaVersion
    }
  };

  const std::filesystem::path packageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path saveAsPackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_save_as_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path stewardPackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_steward_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path stewardMediaPackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_steward_media_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
      storage::CurrentProjectPackageSchemaVersion
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
  GRAPPLE_REQUIRE(!inspectedImage.value().metadata.thumbnailPath.has_value());
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
  GRAPPLE_REQUIRE(!inspectedAudio.value().metadata.thumbnailPath.has_value());
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
      storage::CurrentProjectPackageSchemaVersion
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
      storage::CurrentProjectPackageSchemaVersion
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
      storage::CurrentProjectPackageSchemaVersion
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
  GRAPPLE_REQUIRE(!assetViewModel.value().assets.rows[0].thumbnailPath.has_value());
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
  const timeline::Transform2D clipTransform{
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
  const foundation::NodeId textClipNodeId = assetWriter.nextNodeId("text_clip");
  const auto textClip = assetWriter.apply(
    project::CreateTextClipCommand{
      textClipNodeId,
      assetTrackNodeId,
      assetWriter.nextEdgeId("contains text clip"),
      timeline::TextClipPayload{
        "Native Text",
        foundation::TimeRange{foundation::TimeSeconds{3.0}, foundation::TimeSeconds{6.0}},
        timeline::Transform2D{
          foundation::Vec2{0.0, 0.4},
          foundation::Vec2{1.0, 1.0},
          0.0,
          1.0
        },
        timeline::TextClipStyle{52.0, foundation::Vec3{1.0, 0.9, 0.2}}
      },
      1
    },
    userSource()
  );
  GRAPPLE_REQUIRE(textClip);
  const auto textClipViewModel = assetSession.buildViewModel();
  GRAPPLE_REQUIRE(textClipViewModel);
  GRAPPLE_REQUIRE(textClipViewModel.value().timeline.layers[0].clipCount == 2);
  GRAPPLE_REQUIRE(textClipViewModel.value().timeline.textClips.size() == 1);
  GRAPPLE_REQUIRE(textClipViewModel.value().timeline.textClips[0].sourceNodeId == textClipNodeId);
  GRAPPLE_REQUIRE(textClipViewModel.value().timeline.textClips[0].text == "Native Text");
  GRAPPLE_REQUIRE(textClipViewModel.value().timeline.textClips[0].style.fontSize == 52.0);
  auto textWorkspace = app::NativeWorkspaceSession::fromProject(std::move(assetSession));
  GRAPPLE_REQUIRE(textWorkspace);
  const auto textRefresh = textWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(textRefresh);
  const auto textFrame = textWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(textFrame);
  GRAPPLE_REQUIRE(textFrame.value().frame.mediaFrames.empty());
  GRAPPLE_REQUIRE(textFrame.value().frame.textFrames.size() == 1);
  GRAPPLE_REQUIRE(textFrame.value().frame.textFrames[0].text == "Native Text");

  const std::filesystem::path cacheImagePath = writeTinyPpm("grapple_native_cache_image");
  app::NativeProjectSession cacheProject{
    foundation::ProjectId{"proj_app_cache"},
    "Cache App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_cache"},
      foundation::FilePath{cachePackageRoot.string()},
      storage::CurrentProjectPackageSchemaVersion
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
        timeline::Transform2D{}
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
  const std::thread::id workspaceCallerThread = std::this_thread::get_id();
  std::thread::id workspaceJobThread;
  const auto enqueueWorkspaceJob = cacheWorkspace.value().jobs().enqueue(jobs::Job{
    foundation::JobId{"job_workspace_app"},
    "Workspace App Job",
    [&](jobs::CancellationToken&, jobs::IProgressSink& progress) {
      workspaceJobThread = std::this_thread::get_id();
      progress.reportProgress(1.0);
      return foundation::Result<void>{};
    }
  });
  GRAPPLE_REQUIRE(enqueueWorkspaceJob);
  cacheWorkspace.value().jobs().waitUntilIdle();
  const auto workspaceJobRuns = cacheWorkspace.value().jobs().runRecords();
  GRAPPLE_REQUIRE(workspaceJobThread != workspaceCallerThread);
  GRAPPLE_REQUIRE(workspaceJobRuns.size() == 1);
  GRAPPLE_REQUIRE(workspaceJobRuns[0].jobId == foundation::JobId{"job_workspace_app"});
  GRAPPLE_REQUIRE(workspaceJobRuns[0].status == jobs::JobRunStatus::Succeeded);
  const auto workspaceJobProgress = cacheWorkspace.value().jobs().progressRecords();
  GRAPPLE_REQUIRE(workspaceJobProgress.size() == 1);
  GRAPPLE_REQUIRE(workspaceJobProgress[0].jobId == foundation::JobId{"job_workspace_app"});
  GRAPPLE_REQUIRE(workspaceJobProgress[0].progress == 1.0);
  auto exportOnlyPlan = cacheWorkspace.value().project().buildRenderPlan();
  GRAPPLE_REQUIRE(exportOnlyPlan);
  CapturingRenderRangeSink exportOnlySink;
  const auto exportOnlyResult = cacheWorkspace.value().exportSession().renderPlan(exportOnlyPlan.value().plan, render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{1, 1},
    foundation::Resolution{2, 1},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/cache-export.mov"}
  }, &exportOnlySink);
  GRAPPLE_REQUIRE(exportOnlyResult);
  GRAPPLE_REQUIRE(exportOnlyResult.value().framesEvaluated == 1);
  GRAPPLE_REQUIRE(exportOnlySink.frameImages.size() == 1);
  GRAPPLE_REQUIRE(exportOnlySink.frameImages[0].has_value());
  GRAPPLE_REQUIRE((exportOnlySink.frameImages[0]->resolution == foundation::Resolution{2, 1}));
  GRAPPLE_REQUIRE((exportOnlySink.frameImages[0]->rgbaPixels == std::vector<std::uint8_t>{
    10, 20, 30, 255,
    40, 50, 60, 255
  }));
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 1);
  const std::filesystem::path videoExportPath =
    std::filesystem::temp_directory_path() /
    ("grapple_native_video_export_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".avi");
  const auto videoExport = cacheWorkspace.value().exportSession().renderPlanToVideo(
    exportOnlyPlan.value().plan,
    render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::FrameRate{1, 1},
      foundation::Resolution{16, 16},
      render::Codec{"mjpeg"},
      render::RenderQuality::Final,
      foundation::FilePath{videoExportPath.string()}
    }
  );
  GRAPPLE_REQUIRE(videoExport);
  GRAPPLE_REQUIRE(videoExport.value().framesEvaluated == 1);
  GRAPPLE_REQUIRE(std::filesystem::exists(videoExportPath));
  GRAPPLE_REQUIRE(std::filesystem::file_size(videoExportPath) > 0);
  std::filesystem::remove(videoExportPath);
  const std::filesystem::path progressVideoExportPath =
    std::filesystem::temp_directory_path() /
    ("grapple_native_video_export_progress_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".avi");
  auto cacheExportPlan = cacheWorkspace.value().project().buildRenderPlan();
  GRAPPLE_REQUIRE(cacheExportPlan);
  RecordingProgressSink exportProgress;
  const auto progressVideoExport = cacheWorkspace.value().exportSession().renderPlanToVideo(
    cacheExportPlan.value().plan,
    render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::FrameRate{2, 1},
      foundation::Resolution{16, 16},
      render::Codec{"mjpeg"},
      render::RenderQuality::Final,
      foundation::FilePath{progressVideoExportPath.string()}
    },
    &exportProgress
  );
  GRAPPLE_REQUIRE(progressVideoExport);
  GRAPPLE_REQUIRE(progressVideoExport.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE((exportProgress.records == std::vector<double>{0.5, 1.0}));
  GRAPPLE_REQUIRE(std::filesystem::exists(progressVideoExportPath));
  GRAPPLE_REQUIRE(std::filesystem::file_size(progressVideoExportPath) > 0);
  std::filesystem::remove(progressVideoExportPath);
  const std::filesystem::path cancelledVideoExportPath =
    std::filesystem::temp_directory_path() /
    ("grapple_native_video_export_cancelled_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".avi");
  auto cancelledExportPlan = cacheWorkspace.value().project().buildRenderPlan();
  GRAPPLE_REQUIRE(cancelledExportPlan);
  jobs::CancellationToken exportCancellation;
  CancellingProgressSink cancellingProgress{exportCancellation};
  const auto cancelledVideoExport = cacheWorkspace.value().exportSession().renderPlanToVideo(
    cancelledExportPlan.value().plan,
    render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::FrameRate{2, 1},
      foundation::Resolution{16, 16},
      render::Codec{"mjpeg"},
      render::RenderQuality::Final,
      foundation::FilePath{cancelledVideoExportPath.string()}
    },
    &cancellingProgress,
    &exportCancellation
  );
  GRAPPLE_REQUIRE(!cancelledVideoExport);
  GRAPPLE_REQUIRE(cancelledVideoExport.error().code == "app.export_cancelled");
  GRAPPLE_REQUIRE((cancellingProgress.records == std::vector<double>{0.5}));
  std::filesystem::remove(cancelledVideoExportPath);
  const auto cacheRefresh = cacheWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(cacheRefresh);
  const auto firstCachedFrame = cacheWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(firstCachedFrame);
  GRAPPLE_REQUIRE(firstCachedFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE(firstCachedFrame.value().frame.image->rgbaPixels == exportOnlySink.frameImages[0]->rgbaPixels);
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 3);
  const auto secondCachedFrame = cacheWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(secondCachedFrame);
  GRAPPLE_REQUIRE(cacheWorkspace.value().cachedMediaFrameCount() == 3);
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
      storage::CurrentProjectPackageSchemaVersion
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
  const timeline::Transform2D effectCameraTransform{
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
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          effectCameraTransform,
          timeline::CameraLens{35.0}
        }
      }
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
  GRAPPLE_REQUIRE(effectViewModel.value().timeline.cameras[0].state.transform == effectCameraTransform);
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
  GRAPPLE_REQUIRE(appKeyframeUpsert.value().changed);
  const auto keyframedEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(keyframedEffectViewModel);
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].keyframeId == foundation::KeyframeId{"key_target_x_2"});
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].time == foundation::TimeSeconds{1.25});
  GRAPPLE_REQUIRE(std::get<double>(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].value) == 0.8);
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].lastEditedRevision == appKeyframeUpsert.value().snapshot.revision);
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].lastEditedSourceKind == "user");
  GRAPPLE_REQUIRE(keyframedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].lastEditedActorName == "test");
  const std::size_t commandCountBeforeNoopKeyframeUpsert = effectSession.packageState().commandLog.records().size();
  const auto noopKeyframeUpsert = effectEdits.upsertParamKeyframe(
    effectNodeId,
    "target_x",
    timeline::Param::Keyframe{
      foundation::KeyframeId{"key_target_x_2"},
      foundation::TimeSeconds{1.25},
      0.8
    },
    userSource()
  );
  GRAPPLE_REQUIRE(noopKeyframeUpsert);
  GRAPPLE_REQUIRE(!noopKeyframeUpsert.value().changed);
  GRAPPLE_REQUIRE(!noopKeyframeUpsert.value().committed.has_value());
  GRAPPLE_REQUIRE(noopKeyframeUpsert.value().snapshot.revision == appKeyframeUpsert.value().snapshot.revision);
  GRAPPLE_REQUIRE(effectSession.packageState().commandLog.records().size() == commandCountBeforeNoopKeyframeUpsert);
  app::NativeProjectCommandWriter sparseKeyframeWriter{effectSession};
  GRAPPLE_REQUIRE(sparseKeyframeWriter.nextKeyframeId("target x") == foundation::KeyframeId{"key_target_x_3"});
  const auto appParamValueUpdate = effectEdits.setParamValue(
    effectNodeId,
    "target_x",
    0.6,
    userSource()
  );
  GRAPPLE_REQUIRE(appParamValueUpdate);
  GRAPPLE_REQUIRE(appParamValueUpdate.value().changed);
  const auto valueUpdatedEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel);
  GRAPPLE_REQUIRE(std::get<double>(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.6);
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedRevision == appParamValueUpdate.value().snapshot.revision);
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedSourceKind == "user");
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedActorName == "test");
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 1);
  GRAPPLE_REQUIRE(valueUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].keyframeId == foundation::KeyframeId{"key_target_x_2"});
  const std::size_t commandCountBeforeNoopParamUpdate = effectSession.packageState().commandLog.records().size();
  const auto noopParamValueUpdate = effectEdits.setParamValue(
    effectNodeId,
    "target_x",
    0.6,
    userSource()
  );
  GRAPPLE_REQUIRE(noopParamValueUpdate);
  GRAPPLE_REQUIRE(!noopParamValueUpdate.value().changed);
  GRAPPLE_REQUIRE(!noopParamValueUpdate.value().committed.has_value());
  GRAPPLE_REQUIRE(noopParamValueUpdate.value().snapshot.revision == appParamValueUpdate.value().snapshot.revision);
  GRAPPLE_REQUIRE(effectSession.packageState().commandLog.records().size() == commandCountBeforeNoopParamUpdate);
  const auto boolParamUpdate = effectEdits.setParamValue(
    effectNodeId,
    "lock_subject",
    false,
    userSource()
  );
  GRAPPLE_REQUIRE(boolParamUpdate);
  GRAPPLE_REQUIRE(boolParamUpdate.value().changed);
  const auto boolUpdatedEffectViewModel = effectSession.buildViewModel();
  GRAPPLE_REQUIRE(boolUpdatedEffectViewModel);
  GRAPPLE_REQUIRE(!std::get<bool>(boolUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value));
  GRAPPLE_REQUIRE(boolUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].lastEditedRevision == boolParamUpdate.value().snapshot.revision);
  GRAPPLE_REQUIRE(boolUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].lastEditedSourceKind == "user");
  GRAPPLE_REQUIRE(boolUpdatedEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].lastEditedActorName == "test");
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

  app::NativeProjectSession stewardMediaProject{
    foundation::ProjectId{"proj_app_steward_media"},
    "Steward Media Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_media"},
      foundation::FilePath{stewardMediaPackageRoot.string()},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto stewardMediaWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardMediaProject));
  GRAPPLE_REQUIRE(stewardMediaWorkspace);
  const auto stewardMediaAsset = stewardMediaWorkspace.value().commandWriter().apply(
    project::RegisterAssetCommand{
      asset::Asset{
        foundation::AssetId{"asset_steward_media_video"},
        "Steward Video",
        asset::AssetMetadata{
          asset::AssetMediaType::Video,
          foundation::FilePath{"/tmp/steward-video.avi"},
          std::nullopt,
          foundation::TimeSeconds{6.0},
          foundation::Resolution{640, 360},
          foundation::FrameRate{30, 1}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardMediaAsset);
  const auto stewardMediaPlacement = stewardMediaWorkspace.value().steward().placeAssetOnTimeline(
    foundation::AssetId{"asset_steward_media_video"}
  );
  GRAPPLE_REQUIRE(stewardMediaPlacement);
  GRAPPLE_REQUIRE(stewardMediaPlacement.value().clipNodeId == foundation::NodeId{"node_clip_4"});
  const agent::AgentConversationState stewardMediaConversation =
    stewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardMediaConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardMediaConversation.runs.size() == 1);
  GRAPPLE_REQUIRE(stewardMediaConversation.runs[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardMediaConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardMediaConversation.runs[0].toolCalls[0].toolSerializedId == "timeline.place_asset");
  GRAPPLE_REQUIRE(stewardMediaConversation.runs[0].toolCalls[0].toolDisplayName == "Place Asset On Timeline");
  GRAPPLE_REQUIRE(stewardMediaConversation.runs[0].toolCalls[0].status == agent::AgentConversationToolCallStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardMediaConversation.runs[0].toolCalls[0].observedRevision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.has_value());
  GRAPPLE_REQUIRE(
    stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.value() ==
    stewardMediaConversation.runs[0].runId
  );
  const auto stewardMediaViewModel = stewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardMediaViewModel);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().timeline.compositions.size() == 1);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().timeline.layers.size() == 1);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().timeline.cameras.size() == 1);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().timeline.clips[0].sourceNodeId == stewardMediaPlacement.value().clipNodeId);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().timeline.duration == foundation::TimeSeconds{6.0});
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().steward.edits[0].revision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().steward.edits[0].targetNodeId == stewardMediaPlacement.value().clipNodeId);
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().steward.edits[0].targetName == "Steward Video");
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().steward.edits[0].editName == "Timeline Placement");
  GRAPPLE_REQUIRE(stewardMediaViewModel.value().steward.edits[0].intent == "Add Steward Video to the timeline.");
  const auto stewardClipTransform = stewardMediaWorkspace.value().steward().editClip(
    stewardMediaPlacement.value().clipNodeId,
    "Move clip right and make it smaller."
  );
  GRAPPLE_REQUIRE(stewardClipTransform);
  const agent::AgentConversationState stewardClipTransformConversation =
    stewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardClipTransformConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardClipTransformConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardClipTransformConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardClipTransformConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardClipTransformConversation.runs[1].toolCalls[0].toolSerializedId == "timeline.update_clip_transform");
  GRAPPLE_REQUIRE(stewardClipTransformConversation.runs[1].toolCalls[0].toolDisplayName == "Update Clip Transform");
  GRAPPLE_REQUIRE(stewardClipTransformConversation.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_clip_transform_2"});
  GRAPPLE_REQUIRE(stewardClipTransformConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stewardClipTransform.value().snapshot.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.has_value());
  GRAPPLE_REQUIRE(
    stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.value() ==
    stewardClipTransformConversation.runs[1].runId
  );
  const auto stewardClipTransformViewModel = stewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardClipTransformViewModel);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips[0].transform.position.x == 0.25);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips[0].transform.position.y == 0.0);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips[0].transform.scale.x == 0.75);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips[0].transform.scale.y == 0.75);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips[0].transform.rotationDegrees == 0.0);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips[0].transform.opacity == 1.0);
  GRAPPLE_REQUIRE((stewardClipTransformViewModel.value().timeline.clips[0].timelineRange == foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{6.0}}));
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().timeline.clips[0].assetId == foundation::AssetId{"asset_steward_media_video"});
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().steward.edits[1].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().steward.edits[1].targetNodeId == stewardMediaPlacement.value().clipNodeId);
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().steward.edits[1].targetName == "Steward Video");
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().steward.edits[1].editName == "Clip Transform");
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().steward.edits[1].intent == "Move clip right and make it smaller.");
  GRAPPLE_REQUIRE(stewardClipTransformViewModel.value().steward.edits[1].controlSummary == "Position=0.25, 0, Scale=0.75, 0.75, Rotation=0, Opacity=1");
  const auto stewardClipSpeed = stewardMediaWorkspace.value().steward().editClip(
    stewardMediaPlacement.value().clipNodeId,
    "Speed up selected clip."
  );
  GRAPPLE_REQUIRE(stewardClipSpeed);
  const agent::AgentConversationState stewardClipSpeedConversation =
    stewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.runs.size() == 3);
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.runs[2].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.runs[2].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.runs[2].toolCalls[0].toolSerializedId == "timeline.update_clip_playback_rate");
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.runs[2].toolCalls[0].toolDisplayName == "Update Clip Playback Rate");
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.runs[2].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_clip_playback_rate_3"});
  GRAPPLE_REQUIRE(stewardClipSpeedConversation.runs[2].toolCalls[0].observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardClipSpeed.value().snapshot.revision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.has_value());
  GRAPPLE_REQUIRE(
    stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.value() ==
    stewardClipSpeedConversation.runs[2].runId
  );
  const auto stewardClipSpeedViewModel = stewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel);
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().timeline.clips[0].transform.position.x == 0.25);
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().timeline.clips[0].transform.scale.x == 0.75);
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().timeline.clips[0].playbackRate == 1.25);
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().steward.edits.size() == 3);
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().steward.edits[2].revision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().steward.edits[2].targetNodeId == stewardMediaPlacement.value().clipNodeId);
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().steward.edits[2].targetName == "Steward Video");
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().steward.edits[2].editName == "Clip Playback Rate");
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().steward.edits[2].intent == "Speed up selected clip.");
  GRAPPLE_REQUIRE(stewardClipSpeedViewModel.value().steward.edits[2].controlSummary == "Speed=1.25x");
  const auto stewardClipMove = stewardMediaWorkspace.value().steward().editClip(
    stewardMediaPlacement.value().clipNodeId,
    "Move selected clip later."
  );
  GRAPPLE_REQUIRE(stewardClipMove);
  const agent::AgentConversationState stewardClipMoveConversation =
    stewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardClipMoveConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardClipMoveConversation.runs.size() == 4);
  GRAPPLE_REQUIRE(stewardClipMoveConversation.runs[3].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardClipMoveConversation.runs[3].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardClipMoveConversation.runs[3].toolCalls[0].toolSerializedId == "timeline.move_clip");
  GRAPPLE_REQUIRE(stewardClipMoveConversation.runs[3].toolCalls[0].toolDisplayName == "Move Timeline Clip");
  GRAPPLE_REQUIRE(stewardClipMoveConversation.runs[3].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_clip_move_4"});
  GRAPPLE_REQUIRE(stewardClipMoveConversation.runs[3].toolCalls[0].observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(stewardClipMove.value().snapshot.revision == foundation::RevisionId{"rev_5"});
  const auto stewardClipMoveViewModel = stewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardClipMoveViewModel);
  GRAPPLE_REQUIRE(stewardClipMoveViewModel.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE((stewardClipMoveViewModel.value().timeline.clips[0].timelineRange == foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{7.0}}));
  GRAPPLE_REQUIRE((stewardClipMoveViewModel.value().timeline.clips[0].sourceRange == foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{6.0}}));
  GRAPPLE_REQUIRE(stewardClipMoveViewModel.value().steward.edits.size() == 4);
  GRAPPLE_REQUIRE(stewardClipMoveViewModel.value().steward.edits[3].editName == "Clip Timing");
  GRAPPLE_REQUIRE(stewardClipMoveViewModel.value().steward.edits[3].intent == "Move selected clip later.");
  GRAPPLE_REQUIRE(stewardClipMoveViewModel.value().steward.edits[3].controlSummary == "Start=1s");
  const auto stewardClipTrim = stewardMediaWorkspace.value().steward().editClip(
    stewardMediaPlacement.value().clipNodeId,
    "Shorten selected clip."
  );
  GRAPPLE_REQUIRE(stewardClipTrim);
  const agent::AgentConversationState stewardClipTrimConversation =
    stewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardClipTrimConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardClipTrimConversation.runs.size() == 5);
  GRAPPLE_REQUIRE(stewardClipTrimConversation.runs[4].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardClipTrimConversation.runs[4].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardClipTrimConversation.runs[4].toolCalls[0].toolSerializedId == "timeline.trim_clip");
  GRAPPLE_REQUIRE(stewardClipTrimConversation.runs[4].toolCalls[0].toolDisplayName == "Trim Timeline Clip");
  GRAPPLE_REQUIRE(stewardClipTrimConversation.runs[4].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_clip_trim_5"});
  GRAPPLE_REQUIRE(stewardClipTrimConversation.runs[4].toolCalls[0].observedRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(stewardClipTrim.value().snapshot.revision == foundation::RevisionId{"rev_6"});
  const auto stewardClipTrimViewModel = stewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardClipTrimViewModel);
  GRAPPLE_REQUIRE(stewardClipTrimViewModel.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE((stewardClipTrimViewModel.value().timeline.clips[0].timelineRange == foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{6.0}}));
  GRAPPLE_REQUIRE((stewardClipTrimViewModel.value().timeline.clips[0].sourceRange == foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.75}}));
  GRAPPLE_REQUIRE(stewardClipTrimViewModel.value().timeline.clips[0].playbackRate == 1.25);
  GRAPPLE_REQUIRE(stewardClipTrimViewModel.value().steward.edits.size() == 5);
  GRAPPLE_REQUIRE(stewardClipTrimViewModel.value().steward.edits[4].editName == "Clip Timing");
  GRAPPLE_REQUIRE(stewardClipTrimViewModel.value().steward.edits[4].intent == "Shorten selected clip.");
  GRAPPLE_REQUIRE(stewardClipTrimViewModel.value().steward.edits[4].controlSummary == "Range=1s - 6s, Source=0s - 4.75s");
  const auto stewardTextClip = stewardMediaWorkspace.value().steward().createTextClip(
    "Add title \"Opening Title\".",
    foundation::TimeSeconds{1.0}
  );
  GRAPPLE_REQUIRE(stewardTextClip);
  GRAPPLE_REQUIRE(stewardTextClip.value().textClipNodeId == foundation::NodeId{"node_text_clip_5"});
  const agent::AgentConversationState stewardTextConversation =
    stewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardTextConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardTextConversation.runs.size() == 6);
  GRAPPLE_REQUIRE(stewardTextConversation.runs[5].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardTextConversation.runs[5].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardTextConversation.runs[5].toolCalls[0].toolSerializedId == "timeline.create_text_clip");
  GRAPPLE_REQUIRE(stewardTextConversation.runs[5].toolCalls[0].toolDisplayName == "Create Text Clip");
  GRAPPLE_REQUIRE(stewardTextConversation.runs[5].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_create_text_clip_6"});
  GRAPPLE_REQUIRE(stewardTextConversation.runs[5].toolCalls[0].observedRevision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(stewardTextClip.value().packageResult.snapshot.revision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.has_value());
  GRAPPLE_REQUIRE(
    stewardMediaWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.value() ==
    stewardTextConversation.runs[5].runId
  );
  const auto stewardTextViewModel = stewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardTextViewModel);
  GRAPPLE_REQUIRE(stewardTextViewModel.value().timeline.textClips.size() == 1);
  GRAPPLE_REQUIRE(stewardTextViewModel.value().timeline.textClips[0].sourceNodeId == stewardTextClip.value().textClipNodeId);
  GRAPPLE_REQUIRE(stewardTextViewModel.value().timeline.textClips[0].text == "Opening Title");
  GRAPPLE_REQUIRE((stewardTextViewModel.value().timeline.textClips[0].timelineRange == foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{4.0}}));
  GRAPPLE_REQUIRE(stewardTextViewModel.value().timeline.textClips[0].transform.position.y == 0.35);
  GRAPPLE_REQUIRE(stewardTextViewModel.value().timeline.textClips[0].style.fontSize == 64.0);
  GRAPPLE_REQUIRE(stewardTextViewModel.value().steward.edits.size() == 6);
  GRAPPLE_REQUIRE(stewardTextViewModel.value().steward.edits[5].targetNodeId == stewardTextClip.value().textClipNodeId);
  GRAPPLE_REQUIRE(stewardTextViewModel.value().steward.edits[5].targetName == "Opening Title");
  GRAPPLE_REQUIRE(stewardTextViewModel.value().steward.edits[5].editName == "Text Clip");
  GRAPPLE_REQUIRE(stewardTextViewModel.value().steward.edits[5].intent == "Add title \"Opening Title\".");
  GRAPPLE_REQUIRE(stewardTextViewModel.value().steward.edits[5].controlSummary == "Start=1s, Duration=3s, Font=64");
  const auto stewardTextEdit = stewardMediaWorkspace.value().steward().editTextClip(
    stewardTextClip.value().textClipNodeId,
    "Change title to \"Final Title\" and make font smaller."
  );
  GRAPPLE_REQUIRE(stewardTextEdit);
  const agent::AgentConversationState stewardTextEditConversation =
    stewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardTextEditConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardTextEditConversation.runs.size() == 7);
  GRAPPLE_REQUIRE(stewardTextEditConversation.runs[6].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardTextEditConversation.runs[6].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardTextEditConversation.runs[6].toolCalls[0].toolSerializedId == "timeline.update_text_clip");
  GRAPPLE_REQUIRE(stewardTextEditConversation.runs[6].toolCalls[0].toolDisplayName == "Update Text Clip");
  GRAPPLE_REQUIRE(stewardTextEditConversation.runs[6].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_update_text_clip_7"});
  GRAPPLE_REQUIRE(stewardTextEditConversation.runs[6].toolCalls[0].observedRevision == foundation::RevisionId{"rev_8"});
  GRAPPLE_REQUIRE(stewardTextEdit.value().snapshot.revision == foundation::RevisionId{"rev_8"});
  const auto stewardTextEditViewModel = stewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardTextEditViewModel);
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().timeline.textClips.size() == 1);
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().timeline.textClips[0].text == "Final Title");
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().timeline.textClips[0].style.fontSize == 48.0);
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().steward.edits.size() == 7);
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().steward.edits[6].targetNodeId == stewardTextClip.value().textClipNodeId);
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().steward.edits[6].targetName == "Final Title");
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().steward.edits[6].editName == "Text Clip");
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().steward.edits[6].intent == "Change title to \"Final Title\" and make font smaller.");
  GRAPPLE_REQUIRE(stewardTextEditViewModel.value().steward.edits[6].controlSummary == "Text=Final Title, Font=48");
  const auto stewardMediaWrite = stewardMediaWorkspace.value().writePackage();
  GRAPPLE_REQUIRE(stewardMediaWrite);
  auto reopenedStewardMediaWorkspace =
    app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{stewardMediaPackageRoot.string()});
  GRAPPLE_REQUIRE(reopenedStewardMediaWorkspace);
  const agent::AgentConversationState reopenedStewardMediaConversation =
    reopenedStewardMediaWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs.size() == 7);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[0].title == "Add Steward Video to the timeline.");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[0].toolCalls[0].toolSerializedId == "timeline.place_asset");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[1].title == "Move clip right and make it smaller.");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[1].toolCalls[0].toolSerializedId == "timeline.update_clip_transform");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[2].title == "Speed up selected clip.");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[2].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[2].toolCalls[0].toolSerializedId == "timeline.update_clip_playback_rate");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[2].toolCalls[0].observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[3].title == "Move selected clip later.");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[3].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[3].toolCalls[0].toolSerializedId == "timeline.move_clip");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[3].toolCalls[0].observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[4].title == "Shorten selected clip.");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[4].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[4].toolCalls[0].toolSerializedId == "timeline.trim_clip");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[4].toolCalls[0].observedRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[5].title == "Add title \"Opening Title\".");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[5].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[5].toolCalls[0].toolSerializedId == "timeline.create_text_clip");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[5].toolCalls[0].observedRevision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[6].title == "Change title to \"Final Title\" and make font smaller.");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[6].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[6].toolCalls[0].toolSerializedId == "timeline.update_text_clip");
  GRAPPLE_REQUIRE(reopenedStewardMediaConversation.runs[6].toolCalls[0].observedRevision == foundation::RevisionId{"rev_8"});
  const auto reopenedStewardMediaViewModel = reopenedStewardMediaWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel);
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().project.revision == foundation::RevisionId{"rev_8"});
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().timeline.clips.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().timeline.textClips.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().timeline.textClips[0].text == "Final Title");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().timeline.clips[0].transform.position.x == 0.25);
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().timeline.clips[0].transform.scale.x == 0.75);
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().timeline.clips[0].playbackRate == 1.25);
  GRAPPLE_REQUIRE((reopenedStewardMediaViewModel.value().timeline.clips[0].timelineRange == foundation::TimeRange{foundation::TimeSeconds{1.0}, foundation::TimeSeconds{6.0}}));
  GRAPPLE_REQUIRE((reopenedStewardMediaViewModel.value().timeline.clips[0].sourceRange == foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.75}}));
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits.size() == 7);
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[0].editName == "Timeline Placement");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[1].editName == "Clip Transform");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[1].intent == "Move clip right and make it smaller.");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[2].editName == "Clip Playback Rate");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[2].intent == "Speed up selected clip.");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[3].editName == "Clip Timing");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[3].intent == "Move selected clip later.");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[4].editName == "Clip Timing");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[4].intent == "Shorten selected clip.");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[5].editName == "Text Clip");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[5].intent == "Add title \"Opening Title\".");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[6].editName == "Text Clip");
  GRAPPLE_REQUIRE(reopenedStewardMediaViewModel.value().steward.edits[6].intent == "Change title to \"Final Title\" and make font smaller.");
  std::filesystem::remove_all(stewardMediaPackageRoot);

  const std::filesystem::path stewardDeletePackageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_app_steward_delete_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  app::NativeProjectSession stewardDeleteProject{
    foundation::ProjectId{"proj_app_steward_delete"},
    "Steward Delete Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_delete"},
      foundation::FilePath{stewardDeletePackageRoot.string()},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto stewardDeleteWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardDeleteProject));
  GRAPPLE_REQUIRE(stewardDeleteWorkspace);
  const auto stewardDeleteAsset = stewardDeleteWorkspace.value().commandWriter().apply(
    project::RegisterAssetCommand{
      asset::Asset{
        foundation::AssetId{"asset_steward_delete_video"},
        "Steward Delete Video",
        asset::AssetMetadata{
          asset::AssetMediaType::Video,
          foundation::FilePath{"/tmp/steward-delete-video.avi"},
          std::nullopt,
          foundation::TimeSeconds{4.0},
          foundation::Resolution{640, 360},
          foundation::FrameRate{30, 1}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardDeleteAsset);
  const auto stewardDeletePlacement = stewardDeleteWorkspace.value().steward().placeAssetOnTimeline(
    foundation::AssetId{"asset_steward_delete_video"}
  );
  GRAPPLE_REQUIRE(stewardDeletePlacement);
  const auto stewardDelete = stewardDeleteWorkspace.value().steward().deleteClip(
    stewardDeletePlacement.value().clipNodeId,
    "Delete selected clip."
  );
  GRAPPLE_REQUIRE(stewardDelete);
  GRAPPLE_REQUIRE(stewardDelete.value().snapshot.revision == foundation::RevisionId{"rev_3"});
  const agent::AgentConversationState stewardDeleteConversation =
    stewardDeleteWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardDeleteConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].summary == "Deleted selected timeline clip.");
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].messages.size() == 1);
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].messages[0].content == "Deleting the selected timeline clip.");
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].toolCalls[0].toolSerializedId == "timeline.delete_clip");
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].toolCalls[0].toolDisplayName == "Delete Timeline Clip");
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_delete_clip_2"});
  GRAPPLE_REQUIRE(stewardDeleteConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_3"});
  const auto stewardDeleteViewModel = stewardDeleteWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardDeleteViewModel);
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().timeline.clips.empty());
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().steward.edits[1].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().steward.edits[1].targetNodeId == stewardDeletePlacement.value().clipNodeId);
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().steward.edits[1].targetName == "Steward Delete Video");
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().steward.edits[1].editName == "Clip Delete");
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().steward.edits[1].intent == "Delete selected clip.");
  GRAPPLE_REQUIRE(stewardDeleteViewModel.value().steward.edits[1].controlSummary == "Deleted");
  std::filesystem::remove_all(stewardDeletePackageRoot);

  app::NativeProjectSession stewardDeleteEffectProject{
    foundation::ProjectId{"proj_app_steward_delete_effect"},
    "Steward Delete Effect Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_delete_effect"},
      foundation::FilePath{"steward-delete-effect-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto stewardDeleteEffectWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardDeleteEffectProject));
  GRAPPLE_REQUIRE(stewardDeleteEffectWorkspace);
  const foundation::NodeId stewardDeleteEffectCompositionNodeId =
    stewardDeleteEffectWorkspace.value().commandWriter().nextNodeId("composition");
  const auto stewardDeleteEffectComposition = stewardDeleteEffectWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{stewardDeleteEffectCompositionNodeId, "Delete Effect Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(stewardDeleteEffectComposition);
  const foundation::NodeId stewardDeleteEffectCameraNodeId =
    stewardDeleteEffectWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardDeleteEffectCamera = stewardDeleteEffectWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardDeleteEffectCameraNodeId,
      stewardDeleteEffectCompositionNodeId,
      stewardDeleteEffectWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardDeleteEffectCamera);
  const auto stewardCreatedEffect = stewardDeleteEffectWorkspace.value().steward().createCameraTransformEffect(
    stewardDeleteEffectCameraNodeId,
    "Create editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}}
  );
  GRAPPLE_REQUIRE(stewardCreatedEffect);
  const auto stewardDeletedEffect = stewardDeleteEffectWorkspace.value().steward().deleteCameraTransformEffect(
    stewardDeleteEffectCameraNodeId,
    "Remove camera controls."
  );
  GRAPPLE_REQUIRE(stewardDeletedEffect);
  GRAPPLE_REQUIRE(stewardDeletedEffect.value().snapshot.revision == foundation::RevisionId{"rev_4"});
  const agent::AgentConversationState stewardDeleteEffectConversation =
    stewardDeleteEffectWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].summary == "Deleted Camera Transform controls.");
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].messages.size() == 1);
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].messages[0].content == "Deleting the Camera Transform controls.");
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].toolCalls[0].toolSerializedId == "effect.delete_node");
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].toolCalls[0].toolDisplayName == "Delete Effect Node");
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_delete_effect_2"});
  GRAPPLE_REQUIRE(stewardDeleteEffectConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_4"});
  const auto stewardDeleteEffectViewModel = stewardDeleteEffectWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel);
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().timeline.effectGraphs.empty());
  GRAPPLE_REQUIRE(!app::cameraHasTransformEffect(stewardDeleteEffectViewModel.value(), stewardDeleteEffectCameraNodeId));
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().steward.edits[1].revision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().steward.edits[1].targetNodeId == stewardDeleteEffectCameraNodeId);
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().steward.edits[1].targetName == "Camera");
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().steward.edits[1].editName == "Camera Transform Delete");
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().steward.edits[1].intent == "Remove camera controls.");
  GRAPPLE_REQUIRE(stewardDeleteEffectViewModel.value().steward.edits[1].controlSummary == "Deleted");

  app::NativeProjectSession runtimeProject{
    foundation::ProjectId{"proj_app_runtime"},
    "Runtime App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_runtime"},
      foundation::FilePath{"runtime-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
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
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
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
  GRAPPLE_REQUIRE(duplicateRuntimeEffect.error().code == "agent.camera_transform_exists");
  const agent::AgentConversationState stewardConversation = runtimeWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].title == "Center the subject with an editable camera transform.");
  GRAPPLE_REQUIRE(stewardConversation.runs[0].messages.size() == 1);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls[0].toolSerializedId == "effect.create_node");
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls[0].toolDisplayName == "Create Effect Node");
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls[0].status == agent::AgentConversationToolCallStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardConversation.runs[0].toolCalls[0].observedRevision == runtimeEffect.value().snapshot.revision);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].status == agent::AgentRunStatus::Failed);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].toolCalls.empty());
  GRAPPLE_REQUIRE(stewardConversation.runs[1].diagnostics.size() == 1);
  GRAPPLE_REQUIRE(stewardConversation.runs[1].diagnostics[0].code == "agent.camera_transform_exists");
  GRAPPLE_REQUIRE(runtimeWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.has_value());
  GRAPPLE_REQUIRE(runtimeWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.value() == stewardConversation.runs[0].runId);
  const auto runtimeEffectViewModel = runtimeWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(runtimeEffectViewModel);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].targetName == "Camera");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].displayName == "Camera Transform");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].createdRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].createdSourceKind == "agent");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].createdActorName == "steward");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].createdIntent == "Center the subject with an editable camera transform.");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].implementationKind == "builtin");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].cameraTransformEffect);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params.size() == 3);
  GRAPPLE_REQUIRE(app::stewardCameraTargetId(runtimeEffectViewModel.value(), std::nullopt) == runtimeCameraNodeId);
  GRAPPLE_REQUIRE(app::cameraHasTransformEffect(runtimeEffectViewModel.value(), runtimeCameraNodeId));
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].label == "Position X");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[1].label == "Position Y");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].label == "Zoom");
  GRAPPLE_REQUIRE(std::get<double>(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.0);
  GRAPPLE_REQUIRE(std::get<double>(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].value) == 1.1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].numericMin == 0.25);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].numericMax == 4.0);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].numericStep == 0.01);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].targetNodeId == runtimeCameraNodeId);
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].targetName == "Camera");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].editName == "Camera Transform");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].intent == "Center the subject with an editable camera transform.");
  GRAPPLE_REQUIRE(runtimeEffectViewModel.value().steward.edits[0].controlSummary == "Position X=0, Position Y=0, Zoom=1.1");
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
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].state.transform.position.x == 0.0);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].state.transform.position.y == 0.0);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].state.transform.scale.x == 1.1);
  GRAPPLE_REQUIRE(initialRuntimeFrame.value().frame.cameras[0].state.transform.scale.y == 1.1);

  app::NativeProjectSession wordBoundaryProject{
    foundation::ProjectId{"proj_app_word_boundary"},
    "Word Boundary Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_word_boundary"},
      foundation::FilePath{"word-boundary-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto wordBoundaryWorkspace = app::NativeWorkspaceSession::fromProject(std::move(wordBoundaryProject));
  GRAPPLE_REQUIRE(wordBoundaryWorkspace);
  const foundation::NodeId wordBoundaryCompositionNodeId =
    wordBoundaryWorkspace.value().commandWriter().nextNodeId("composition");
  const auto wordBoundaryComposition = wordBoundaryWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{wordBoundaryCompositionNodeId, "Word Boundary Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(wordBoundaryComposition);
  const foundation::NodeId wordBoundaryCameraNodeId =
    wordBoundaryWorkspace.value().commandWriter().nextNodeId("camera");
  const auto wordBoundaryCamera = wordBoundaryWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      wordBoundaryCameraNodeId,
      wordBoundaryCompositionNodeId,
      wordBoundaryWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(wordBoundaryCamera);
  const auto wordBoundaryEffect = wordBoundaryWorkspace.value().steward().createCameraTransformEffect(
    wordBoundaryCameraNodeId,
    "Make a bright setup around the subject with editable controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
  );
  GRAPPLE_REQUIRE(wordBoundaryEffect);
  const auto wordBoundaryViewModel = wordBoundaryWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(wordBoundaryViewModel);
  GRAPPLE_REQUIRE(wordBoundaryViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(wordBoundaryViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(std::get<double>(wordBoundaryViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.0);
  GRAPPLE_REQUIRE(std::get<double>(wordBoundaryViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value) == 0.0);
  GRAPPLE_REQUIRE(std::get<double>(wordBoundaryViewModel.value().timeline.effectGraphs[0].effects[0].params[2].value) == 1.1);

  const foundation::NodeId staticMoveCameraNodeId =
    wordBoundaryWorkspace.value().commandWriter().nextNodeId("camera");
  const auto staticMoveCamera = wordBoundaryWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      staticMoveCameraNodeId,
      wordBoundaryCompositionNodeId,
      wordBoundaryWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Static Move Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(staticMoveCamera);
  const auto staticMoveEffect = wordBoundaryWorkspace.value().steward().createCameraTransformEffect(
    staticMoveCameraNodeId,
    "Move the camera framing right with editable controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}}
  );
  GRAPPLE_REQUIRE(staticMoveEffect);
  const agent::AgentConversationState staticMoveConversation =
    wordBoundaryWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(staticMoveConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(staticMoveConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(staticMoveConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(staticMoveConversation.runs[1].toolCalls[0].toolSerializedId == "effect.create_node");
  const auto staticMoveViewModel = wordBoundaryWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(staticMoveViewModel);
  const app::AppEffectGraphRow* staticMoveGraph = nullptr;
  for (const app::AppEffectGraphRow& graph : staticMoveViewModel.value().timeline.effectGraphs) {
    if (graph.targetNodeId == staticMoveCameraNodeId) {
      staticMoveGraph = &graph;
    }
  }
  GRAPPLE_REQUIRE(staticMoveGraph != nullptr);
  GRAPPLE_REQUIRE(staticMoveGraph->effects.size() == 1);
  GRAPPLE_REQUIRE(std::get<double>(staticMoveGraph->effects[0].params[0].value) == 0.25);
  GRAPPLE_REQUIRE(staticMoveGraph->effects[0].params[0].keyframes.empty());
  GRAPPLE_REQUIRE(std::get<double>(staticMoveGraph->effects[0].params[1].value) == 0.0);
  GRAPPLE_REQUIRE(staticMoveGraph->effects[0].params[1].keyframes.empty());
  GRAPPLE_REQUIRE(std::get<double>(staticMoveGraph->effects[0].params[2].value) == 1.0);
  GRAPPLE_REQUIRE(staticMoveGraph->effects[0].params[2].keyframes.empty());

  const auto updatedRuntimeEffect = runtimeWorkspace.value().effects().setParamValue(
    runtimeEffectNodeId,
    effects::builtin_effect::PositionXParam,
    0.25,
    userSource()
  );
  GRAPPLE_REQUIRE(updatedRuntimeEffect);
  GRAPPLE_REQUIRE(updatedRuntimeEffect.value().changed);
  const auto updatedRuntimeZoom = runtimeWorkspace.value().effects().setParamValue(
    runtimeEffectNodeId,
    effects::builtin_effect::ZoomParam,
    1.5,
    userSource()
  );
  GRAPPLE_REQUIRE(updatedRuntimeZoom);
  GRAPPLE_REQUIRE(updatedRuntimeZoom.value().changed);
  const auto updatedRuntimeEffectViewModel = runtimeWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel);
  GRAPPLE_REQUIRE(std::get<double>(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.25);
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedRevision == updatedRuntimeEffect.value().snapshot.revision);
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedSourceKind == "user");
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedActorName == "test");
  GRAPPLE_REQUIRE(std::get<double>(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].value) == 1.5);
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].lastEditedRevision == updatedRuntimeZoom.value().snapshot.revision);
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].lastEditedSourceKind == "user");
  GRAPPLE_REQUIRE(updatedRuntimeEffectViewModel.value().timeline.effectGraphs[0].effects[0].params[2].lastEditedActorName == "test");
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
  render::LocalRenderSystem countedRenderSystem{countedCore};
  app::NativePreviewSession countedPreview{
    runtimeWorkspace.value().project(),
    countedRenderSystem
  };
  const auto countedRefresh = countedPreview.refreshFromProject();
  GRAPPLE_REQUIRE(countedRefresh);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  const auto repeatedCountedRefresh = countedPreview.refreshFromProject();
  GRAPPLE_REQUIRE(repeatedCountedRefresh);
  GRAPPLE_REQUIRE(repeatedCountedRefresh.value().preparedPlanHash == countedRefresh.value().preparedPlanHash);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  app::NativeExportSession countedExport{countedRenderSystem};
  auto countedPlan = runtimeWorkspace.value().project().buildRenderPlan();
  GRAPPLE_REQUIRE(countedPlan);
  const auto countedExportResult = countedExport.renderPlan(countedPlan.value().plan, render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{1, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-counted-export.mov"}
  });
  GRAPPLE_REQUIRE(countedExportResult);
  GRAPPLE_REQUIRE(countedExportResult.value().sourceRevision == countedPlan.value().plan.revision);
  GRAPPLE_REQUIRE(countedExportResult.value().renderPlanHash == countedRefresh.value().preparedPlanHash);
  GRAPPLE_REQUIRE(countedExport.state().core.preparedPlanHash == countedRefresh.value().preparedPlanHash);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  const auto repeatedCountedExport = countedExport.renderPlan(countedPlan.value().plan, render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{1, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-counted-export-repeat.mov"}
  });
  GRAPPLE_REQUIRE(repeatedCountedExport);
  GRAPPLE_REQUIRE(countedExport.state().core.preparedPlanHash == countedRefresh.value().preparedPlanHash);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  const auto noteOnlyEdit = runtimeWorkspace.value().commandWriter().apply(
    project::CreateNoteCommand{
      runtimeWorkspace.value().commandWriter().nextNodeId("note"),
      timeline::NotePayload{"Render note", "This project note must not invalidate prepared render work."}
    },
    userSource()
  );
  GRAPPLE_REQUIRE(noteOnlyEdit);
  const auto noteOnlyRefresh = countedPreview.refreshFromProject();
  GRAPPLE_REQUIRE(noteOnlyRefresh);
  GRAPPLE_REQUIRE(noteOnlyRefresh.value().revision == noteOnlyEdit.value().snapshot.revision);
  GRAPPLE_REQUIRE(noteOnlyRefresh.value().preparedPlanHash == countedExport.state().core.preparedPlanHash);
  GRAPPLE_REQUIRE(countedRenderSystem.state().core.revision == noteOnlyEdit.value().snapshot.revision);
  GRAPPLE_REQUIRE(countedRuntime.prepareCount == 1);
  const auto renamedCamera = runtimeWorkspace.value().commandWriter().apply(
    project::UpdateCameraCommand{
      runtimeCameraNodeId,
      timeline::CameraPayload{
        "Renamed Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(renamedCamera);
  const auto renamedCameraRefresh = countedPreview.refreshFromProject();
  GRAPPLE_REQUIRE(renamedCameraRefresh);
  GRAPPLE_REQUIRE(renamedCameraRefresh.value().revision == renamedCamera.value().snapshot.revision);
  GRAPPLE_REQUIRE(renamedCameraRefresh.value().preparedPlanHash == countedExport.state().core.preparedPlanHash);
  GRAPPLE_REQUIRE(countedRenderSystem.state().core.revision == renamedCamera.value().snapshot.revision);
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
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].state.transform.position.x == 0.25);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].state.transform.position.y == 0.0);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].state.transform.scale.x == 1.5);
  GRAPPLE_REQUIRE(runtimeFrame.value().frame.cameras[0].state.transform.scale.y == 1.5);
  auto runtimeExportPlan = runtimeWorkspace.value().project().buildRenderPlan();
  GRAPPLE_REQUIRE(runtimeExportPlan);
  const auto runtimeExport = runtimeWorkspace.value().exportSession().renderPlan(
    runtimeExportPlan.value().plan,
    render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::FrameRate{2, 1},
      foundation::Resolution{1920, 1080},
      render::Codec{"mjpeg"},
      render::RenderQuality::Final,
      foundation::FilePath{"/tmp/app-runtime-export.mov"}
    }
  );
  GRAPPLE_REQUIRE(runtimeExport);
  GRAPPLE_REQUIRE(runtimeExport.value().sourceRevision == runtimeExportPlan.value().plan.revision);
  GRAPPLE_REQUIRE(runtimeExport.value().renderPlanHash == runtimeWorkspace.value().exportSession().state().core.preparedPlanHash.value());
  GRAPPLE_REQUIRE(runtimeExport.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(runtimeExport.value().framesEvaluated == 2);

  app::NativeProjectSession stewardAdjustProject{
    foundation::ProjectId{"proj_app_steward_adjust"},
    "Steward Adjust Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_adjust"},
      foundation::FilePath{"steward-adjust-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto stewardAdjustWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardAdjustProject));
  GRAPPLE_REQUIRE(stewardAdjustWorkspace);
  const foundation::NodeId stewardAdjustCompositionNodeId =
    stewardAdjustWorkspace.value().commandWriter().nextNodeId("composition");
  const auto stewardAdjustComposition = stewardAdjustWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{stewardAdjustCompositionNodeId, "Steward Adjust Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(stewardAdjustComposition);
  const foundation::NodeId stewardAdjustCameraNodeId =
    stewardAdjustWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardAdjustCamera = stewardAdjustWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardAdjustCameraNodeId,
      stewardAdjustCompositionNodeId,
      stewardAdjustWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardAdjustCamera);
  const auto stewardAdjustEffect = stewardAdjustWorkspace.value().steward().createCameraTransformEffect(
    stewardAdjustCameraNodeId,
    "Center the subject with editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardAdjustEffect);
  const auto stewardAdjustedControls = stewardAdjustWorkspace.value().steward().adjustCameraTransformControls(
    stewardAdjustCameraNodeId,
    "Move the camera framing right.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardAdjustedControls);
  GRAPPLE_REQUIRE(stewardAdjustedControls.value().snapshot.revision == foundation::RevisionId{"rev_4"});
  const agent::AgentConversationState stewardAdjustConversation =
    stewardAdjustWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardAdjustConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardAdjustConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardAdjustConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardAdjustConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardAdjustConversation.runs[1].toolCalls[0].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardAdjustConversation.runs[1].toolCalls[0].toolDisplayName == "Update Effect Parameter");
  GRAPPLE_REQUIRE(stewardAdjustConversation.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_2_1"});
  GRAPPLE_REQUIRE(stewardAdjustConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_4"});
  const auto stewardAdjustViewModel = stewardAdjustWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardAdjustViewModel);
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().timeline.effectGraphs[0].effects[0].params[0].name == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(std::get<double>(stewardAdjustViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.25);
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedSourceKind == "agent");
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().steward.edits[1].revision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().steward.edits[1].targetNodeId == stewardAdjustCameraNodeId);
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().steward.edits[1].targetName == "Camera");
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().steward.edits[1].editName == "Camera Transform");
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().steward.edits[1].intent == "Move the camera framing right.");
  GRAPPLE_REQUIRE(stewardAdjustViewModel.value().steward.edits[1].controlSummary == "Position X=0.25");
  const auto stewardPannedControls = stewardAdjustWorkspace.value().steward().adjustCameraTransformControls(
    stewardAdjustCameraNodeId,
    "Pan right with existing editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardPannedControls);
  GRAPPLE_REQUIRE(stewardPannedControls.value().snapshot.revision == foundation::RevisionId{"rev_6"});
  const agent::AgentConversationState stewardPanConversation =
    stewardAdjustWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardPanConversation.runs.size() == 3);
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].toolCalls.size() == 2);
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].toolCalls[0].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_3_1"});
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].toolCalls[0].observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].toolCalls[1].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].toolCalls[1].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_3_2"});
  GRAPPLE_REQUIRE(stewardPanConversation.runs[2].toolCalls[1].observedRevision == foundation::RevisionId{"rev_6"});
  const auto stewardPanViewModel = stewardAdjustWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardPanViewModel);
  GRAPPLE_REQUIRE(stewardPanViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(stewardPanViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(stewardPanViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 2);
  GRAPPLE_REQUIRE(stewardPanViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].time == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(std::get<double>(stewardPanViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].value) == 0.25);
  GRAPPLE_REQUIRE(stewardPanViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[1].time == foundation::TimeSeconds{3.0});
  GRAPPLE_REQUIRE(std::get<double>(stewardPanViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[1].value) == 0.5);
  GRAPPLE_REQUIRE(stewardPanViewModel.value().steward.edits.size() == 3);
  GRAPPLE_REQUIRE(stewardPanViewModel.value().steward.edits[2].editName == "Camera Transform Keyframe");
  GRAPPLE_REQUIRE(stewardPanViewModel.value().steward.edits[2].revision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(stewardPanViewModel.value().steward.edits[2].intent == "Pan right with existing editable camera controls.");
  GRAPPLE_REQUIRE(stewardPanViewModel.value().steward.edits[2].controlSummary == "Position X 0s=0.25, Position X 3s=0.5");
  const auto stewardPanRefresh = stewardAdjustWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(stewardPanRefresh);
  const auto stewardPanMidFrame = stewardAdjustWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{1.5},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(stewardPanMidFrame);
  GRAPPLE_REQUIRE(stewardPanMidFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(stewardPanMidFrame.value().frame.cameras.size() == 1);
  GRAPPLE_REQUIRE(stewardPanMidFrame.value().frame.cameras[0].state.transform.position.x == 0.375);

  const auto stewardShiftedKeyedControls = stewardAdjustWorkspace.value().steward().adjustCameraTransformControls(
    stewardAdjustCameraNodeId,
    "Move the camera framing right.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardShiftedKeyedControls);
  GRAPPLE_REQUIRE(stewardShiftedKeyedControls.value().snapshot.revision == foundation::RevisionId{"rev_8"});
  const agent::AgentConversationState stewardShiftConversation =
    stewardAdjustWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardShiftConversation.runs.size() == 4);
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].toolCalls.size() == 2);
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].toolCalls[0].toolSerializedId == "effect.update_param_keyframe");
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_4_1"});
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].toolCalls[0].observedRevision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].toolCalls[1].toolSerializedId == "effect.update_param_keyframe");
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].toolCalls[1].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_4_2"});
  GRAPPLE_REQUIRE(stewardShiftConversation.runs[3].toolCalls[1].observedRevision == foundation::RevisionId{"rev_8"});
  const auto stewardShiftViewModel = stewardAdjustWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardShiftViewModel);
  GRAPPLE_REQUIRE(stewardShiftViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 2);
  GRAPPLE_REQUIRE(std::get<double>(stewardShiftViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].value) == 0.5);
  GRAPPLE_REQUIRE(std::get<double>(stewardShiftViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[1].value) == 0.75);
  GRAPPLE_REQUIRE(stewardShiftViewModel.value().steward.edits.size() == 4);
  GRAPPLE_REQUIRE(stewardShiftViewModel.value().steward.edits[3].editName == "Camera Transform Keyframe");
  GRAPPLE_REQUIRE(stewardShiftViewModel.value().steward.edits[3].revision == foundation::RevisionId{"rev_8"});
  GRAPPLE_REQUIRE(stewardShiftViewModel.value().steward.edits[3].intent == "Move the camera framing right.");
  const auto stewardShiftRefresh = stewardAdjustWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(stewardShiftRefresh);
  const auto stewardShiftMidFrame = stewardAdjustWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{1.5},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(stewardShiftMidFrame);
  GRAPPLE_REQUIRE(stewardShiftMidFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(stewardShiftMidFrame.value().frame.cameras.size() == 1);
  GRAPPLE_REQUIRE(stewardShiftMidFrame.value().frame.cameras[0].state.transform.position.x == 0.625);

  const auto stewardCombinedControls = stewardAdjustWorkspace.value().steward().adjustCameraTransformControls(
    stewardAdjustCameraNodeId,
    "Move the camera up and make the subject bigger.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardCombinedControls);
  GRAPPLE_REQUIRE(stewardCombinedControls.value().snapshot.revision == foundation::RevisionId{"rev_10"});
  const agent::AgentConversationState stewardCombinedConversation =
    stewardAdjustWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs.size() == 5);
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].toolCalls.size() == 2);
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].toolCalls[0].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_5_1"});
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].toolCalls[0].observedRevision == foundation::RevisionId{"rev_9"});
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].toolCalls[1].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].toolCalls[1].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_5_2"});
  GRAPPLE_REQUIRE(stewardCombinedConversation.runs[4].toolCalls[1].observedRevision == foundation::RevisionId{"rev_10"});
  const auto stewardCombinedViewModel = stewardAdjustWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardCombinedViewModel);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[0].name == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[1].name == effects::builtin_effect::PositionYParam);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[2].name == effects::builtin_effect::ZoomParam);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 2);
  GRAPPLE_REQUIRE(std::get<double>(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value) == -0.2);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[1].lastEditedRevision == foundation::RevisionId{"rev_9"});
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[1].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(std::get<double>(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[2].value) == 1.375);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[2].lastEditedRevision == foundation::RevisionId{"rev_10"});
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().timeline.effectGraphs[0].effects[0].params[2].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().steward.edits.size() == 5);
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().steward.edits[4].intent == "Move the camera up and make the subject bigger.");
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().steward.edits[4].revision == foundation::RevisionId{"rev_10"});
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().steward.edits[4].editName == "Camera Transform");
  GRAPPLE_REQUIRE(stewardCombinedViewModel.value().steward.edits[4].controlSummary == "Position Y=-0.2, Zoom=1.375");

  app::NativeProjectSession stewardRecenterProject{
    foundation::ProjectId{"proj_app_steward_recenter"},
    "Steward Recenter Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_recenter"},
      foundation::FilePath{"steward-recenter-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto stewardRecenterWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardRecenterProject));
  GRAPPLE_REQUIRE(stewardRecenterWorkspace);
  const foundation::NodeId stewardRecenterCompositionNodeId =
    stewardRecenterWorkspace.value().commandWriter().nextNodeId("composition");
  const auto stewardRecenterComposition = stewardRecenterWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{stewardRecenterCompositionNodeId, "Steward Recenter Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(stewardRecenterComposition);
  const foundation::NodeId stewardRecenterCameraNodeId =
    stewardRecenterWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardRecenterCamera = stewardRecenterWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardRecenterCameraNodeId,
      stewardRecenterCompositionNodeId,
      stewardRecenterWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardRecenterCamera);
  const auto stewardRecenterEffect = stewardRecenterWorkspace.value().steward().createCameraTransformEffect(
    stewardRecenterCameraNodeId,
    "Move the subject right and down with editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardRecenterEffect);
  const auto stewardRecenteredControls = stewardRecenterWorkspace.value().steward().adjustCameraTransformControls(
    stewardRecenterCameraNodeId,
    "Recenter the subject.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardRecenteredControls);
  GRAPPLE_REQUIRE(stewardRecenteredControls.value().snapshot.revision == foundation::RevisionId{"rev_5"});
  const agent::AgentConversationState stewardRecenterConversation =
    stewardRecenterWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardRecenterConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].toolCalls.size() == 2);
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].toolCalls[0].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_2_1"});
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].toolCalls[1].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].toolCalls[1].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_2_2"});
  GRAPPLE_REQUIRE(stewardRecenterConversation.runs[1].toolCalls[1].observedRevision == foundation::RevisionId{"rev_5"});
  const auto stewardRecenterViewModel = stewardRecenterWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardRecenterViewModel);
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().timeline.effectGraphs[0].effects[0].params[0].name == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().timeline.effectGraphs[0].effects[0].params[1].name == effects::builtin_effect::PositionYParam);
  GRAPPLE_REQUIRE(std::get<double>(stewardRecenterViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.0);
  GRAPPLE_REQUIRE(std::get<double>(stewardRecenterViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value) == 0.0);
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().timeline.effectGraphs[0].effects[0].params[1].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().steward.edits[1].revision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().steward.edits[1].targetNodeId == stewardRecenterCameraNodeId);
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().steward.edits[1].editName == "Camera Transform");
  GRAPPLE_REQUIRE(stewardRecenterViewModel.value().steward.edits[1].intent == "Recenter the subject.");

  app::NativeProjectSession stewardResetProject{
    foundation::ProjectId{"proj_app_steward_reset"},
    "Steward Reset Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_reset"},
      foundation::FilePath{"steward-reset-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto stewardResetWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardResetProject));
  GRAPPLE_REQUIRE(stewardResetWorkspace);
  const foundation::NodeId stewardResetCompositionNodeId =
    stewardResetWorkspace.value().commandWriter().nextNodeId("composition");
  const auto stewardResetComposition = stewardResetWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{stewardResetCompositionNodeId, "Steward Reset Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(stewardResetComposition);
  const foundation::NodeId stewardResetCameraNodeId =
    stewardResetWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardResetCamera = stewardResetWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardResetCameraNodeId,
      stewardResetCompositionNodeId,
      stewardResetWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardResetCamera);
  const auto stewardResetEffect = stewardResetWorkspace.value().steward().createCameraTransformEffect(
    stewardResetCameraNodeId,
    "Move the subject right, down, and bigger with editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardResetEffect);
  const auto stewardResetControls = stewardResetWorkspace.value().steward().adjustCameraTransformControls(
    stewardResetCameraNodeId,
    "Reset the camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}}
  );
  GRAPPLE_REQUIRE(stewardResetControls);
  GRAPPLE_REQUIRE(stewardResetControls.value().snapshot.revision == foundation::RevisionId{"rev_6"});
  const agent::AgentConversationState stewardResetConversation =
    stewardResetWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardResetConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardResetConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls.size() == 3);
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[0].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_2_1"});
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[1].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[1].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_2_2"});
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[1].observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[2].toolSerializedId == "effect.update_param_value");
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[2].toolCallId == foundation::ToolId{"tool_steward_camera_transform_param_2_3"});
  GRAPPLE_REQUIRE(stewardResetConversation.runs[1].toolCalls[2].observedRevision == foundation::RevisionId{"rev_6"});
  const auto stewardResetViewModel = stewardResetWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardResetViewModel);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[0].name == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[1].name == effects::builtin_effect::PositionYParam);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[2].name == effects::builtin_effect::ZoomParam);
  GRAPPLE_REQUIRE(std::get<double>(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[0].value) == 0.0);
  GRAPPLE_REQUIRE(std::get<double>(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[1].value) == 0.0);
  GRAPPLE_REQUIRE(std::get<double>(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[2].value) == 1.0);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[0].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[1].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardResetViewModel.value().timeline.effectGraphs[0].effects[0].params[2].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardResetViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().steward.edits[1].revision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(stewardResetViewModel.value().steward.edits[1].targetNodeId == stewardResetCameraNodeId);
  GRAPPLE_REQUIRE(stewardResetViewModel.value().steward.edits[1].editName == "Camera Transform");
  GRAPPLE_REQUIRE(stewardResetViewModel.value().steward.edits[1].intent == "Reset the camera controls.");

  app::NativeProjectSession stewardMotionProject{
    foundation::ProjectId{"proj_app_steward_motion"},
    "Steward Motion Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_motion"},
      foundation::FilePath{"steward-motion-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  auto stewardMotionWorkspace = app::NativeWorkspaceSession::fromProject(std::move(stewardMotionProject));
  GRAPPLE_REQUIRE(stewardMotionWorkspace);
  const foundation::NodeId stewardMotionCompositionNodeId =
    stewardMotionWorkspace.value().commandWriter().nextNodeId("composition");
  const auto stewardMotionComposition = stewardMotionWorkspace.value().commandWriter().apply(
    project::CreateCompositionCommand{stewardMotionCompositionNodeId, "Steward Motion Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(stewardMotionComposition);
  const foundation::NodeId stewardMotionCameraNodeId =
    stewardMotionWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardMotionCamera = stewardMotionWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardMotionCameraNodeId,
      stewardMotionCompositionNodeId,
      stewardMotionWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardMotionCamera);
  const auto stewardMotionEffect = stewardMotionWorkspace.value().steward().createCameraTransformEffect(
    stewardMotionCameraNodeId,
    "Pan right with editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}}
  );
  GRAPPLE_REQUIRE(stewardMotionEffect);
  GRAPPLE_REQUIRE(stewardMotionEffect.value().snapshot.revision == foundation::RevisionId{"rev_5"});
  const agent::AgentConversationState stewardMotionConversation =
    stewardMotionWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardMotionConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(stewardMotionConversation.runs.size() == 1);
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls.size() == 3);
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[0].toolSerializedId == "effect.create_node");
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[0].toolDisplayName == "Create Effect Node");
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[0].observedRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[1].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[1].toolDisplayName == "Create Effect Param Keyframe");
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[1].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_1_1"});
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[1].observedRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[2].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[2].toolDisplayName == "Create Effect Param Keyframe");
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[2].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_1_2"});
  GRAPPLE_REQUIRE(stewardMotionConversation.runs[0].toolCalls[2].observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(
    stewardMotionWorkspace.value().project().packageState().commandLog.records().back().sourceRunId.value() ==
    stewardMotionConversation.runs[0].runId
  );

  const auto stewardMotionViewModel = stewardMotionWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardMotionViewModel);
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].createdRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].name == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes.size() == 2);
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].time == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(std::get<double>(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].value) == 0.0);
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].lastEditedSourceKind == "agent");
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[0].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[1].time == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(std::get<double>(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[1].value) == 0.25);
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[1].lastEditedSourceKind == "agent");
  GRAPPLE_REQUIRE(stewardMotionViewModel.value().timeline.effectGraphs[0].effects[0].params[0].keyframes[1].lastEditedActorName == "steward");

  const auto stewardMotionRefresh = stewardMotionWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(stewardMotionRefresh);
  const auto stewardMotionMidFrame = stewardMotionWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{2.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(stewardMotionMidFrame);
  GRAPPLE_REQUIRE(stewardMotionMidFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(stewardMotionMidFrame.value().frame.cameras.size() == 1);
  GRAPPLE_REQUIRE(stewardMotionMidFrame.value().frame.cameras[0].cameraNodeId == stewardMotionCameraNodeId);
  GRAPPLE_REQUIRE(stewardMotionMidFrame.value().frame.cameras[0].state.transform.position.x == 0.125);
  const auto stewardMotionExportPlan = stewardMotionWorkspace.value().project().buildRenderPlan();
  GRAPPLE_REQUIRE(stewardMotionExportPlan);
  CapturingRenderRangeSink stewardMotionExportFrames;
  const auto stewardMotionExport = stewardMotionWorkspace.value().exportSession().renderPlan(
    stewardMotionExportPlan.value().plan,
    render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{3.0}},
      foundation::FrameRate{1, 1},
      foundation::Resolution{1920, 1080},
      render::Codec{"mjpeg"},
      render::RenderQuality::Final,
      foundation::FilePath{"/tmp/app-steward-motion-export.mov"}
    },
    &stewardMotionExportFrames
  );
  GRAPPLE_REQUIRE(stewardMotionExport);
  GRAPPLE_REQUIRE(stewardMotionExport.value().sourceRevision == stewardMotionExportPlan.value().plan.revision);
  GRAPPLE_REQUIRE(stewardMotionExport.value().renderPlanHash == stewardMotionMidFrame.value().frame.renderPlanHash);
  GRAPPLE_REQUIRE(stewardMotionExport.value().framesEvaluated == 3);
  GRAPPLE_REQUIRE(stewardMotionExport.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(stewardMotionExportFrames.frameTimes.size() == 3);
  GRAPPLE_REQUIRE(stewardMotionExportFrames.frameTimes[2] == foundation::TimeSeconds{2.0});
  GRAPPLE_REQUIRE(stewardMotionExportFrames.frameCameras[2].size() == 1);
  GRAPPLE_REQUIRE(stewardMotionExportFrames.frameCameras[2][0].cameraNodeId == stewardMotionCameraNodeId);
  GRAPPLE_REQUIRE(stewardMotionExportFrames.frameCameras[2][0].state.transform.position.x == 0.125);

  const foundation::NodeId stewardZoomCameraNodeId =
    stewardMotionWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardZoomCamera = stewardMotionWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardZoomCameraNodeId,
      stewardMotionCompositionNodeId,
      stewardMotionWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Zoom Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardZoomCamera);
  const auto stewardZoomEffect = stewardMotionWorkspace.value().steward().createCameraTransformEffect(
    stewardZoomCameraNodeId,
    "Zoom in over time with editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}}
  );
  GRAPPLE_REQUIRE(stewardZoomEffect);
  GRAPPLE_REQUIRE(stewardZoomEffect.value().snapshot.revision == foundation::RevisionId{"rev_9"});
  const agent::AgentConversationState stewardZoomConversation =
    stewardMotionWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardZoomConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardZoomConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardZoomConversation.runs[1].toolCalls.size() == 3);
  GRAPPLE_REQUIRE(stewardZoomConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(stewardZoomConversation.runs[1].toolCalls[1].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(stewardZoomConversation.runs[1].toolCalls[1].observedRevision == foundation::RevisionId{"rev_8"});
  GRAPPLE_REQUIRE(stewardZoomConversation.runs[1].toolCalls[2].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(stewardZoomConversation.runs[1].toolCalls[2].observedRevision == foundation::RevisionId{"rev_9"});

  const auto stewardZoomViewModel = stewardMotionWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardZoomViewModel);
  const app::AppEffectGraphRow* zoomEffectGraph = nullptr;
  for (const app::AppEffectGraphRow& graph : stewardZoomViewModel.value().timeline.effectGraphs) {
    if (graph.targetNodeId == stewardZoomCameraNodeId) {
      zoomEffectGraph = &graph;
    }
  }
  GRAPPLE_REQUIRE(zoomEffectGraph != nullptr);
  GRAPPLE_REQUIRE(zoomEffectGraph->effects.size() == 1);
  GRAPPLE_REQUIRE(zoomEffectGraph->effects[0].params[2].name == effects::builtin_effect::ZoomParam);
  GRAPPLE_REQUIRE(zoomEffectGraph->effects[0].params[2].keyframes.size() == 2);
  GRAPPLE_REQUIRE(zoomEffectGraph->effects[0].params[2].keyframes[0].time == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(std::get<double>(zoomEffectGraph->effects[0].params[2].keyframes[0].value) == 1.0);
  GRAPPLE_REQUIRE(zoomEffectGraph->effects[0].params[2].keyframes[0].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(zoomEffectGraph->effects[0].params[2].keyframes[1].time == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(std::get<double>(zoomEffectGraph->effects[0].params[2].keyframes[1].value) == 1.5);
  GRAPPLE_REQUIRE(zoomEffectGraph->effects[0].params[2].keyframes[1].lastEditedActorName == "steward");

  const auto stewardZoomRefresh = stewardMotionWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(stewardZoomRefresh);
  const auto stewardZoomMidFrame = stewardMotionWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{2.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(stewardZoomMidFrame);
  GRAPPLE_REQUIRE(stewardZoomMidFrame.value().runtimeDiagnostics.empty());
  const render::RenderedCamera* zoomRenderedCamera = nullptr;
  for (const render::RenderedCamera& camera : stewardZoomMidFrame.value().frame.cameras) {
    if (camera.cameraNodeId == stewardZoomCameraNodeId) {
      zoomRenderedCamera = &camera;
    }
  }
  GRAPPLE_REQUIRE(zoomRenderedCamera != nullptr);
  GRAPPLE_REQUIRE(zoomRenderedCamera->state.transform.scale.x == 1.25);
  GRAPPLE_REQUIRE(zoomRenderedCamera->state.transform.scale.y == 1.25);

  const foundation::NodeId stewardBiggerCameraNodeId =
    stewardMotionWorkspace.value().commandWriter().nextNodeId("camera");
  const auto stewardBiggerCamera = stewardMotionWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      stewardBiggerCameraNodeId,
      stewardMotionCompositionNodeId,
      stewardMotionWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Bigger Subject Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(stewardBiggerCamera);
  const auto stewardBiggerEffect = stewardMotionWorkspace.value().steward().createCameraTransformEffect(
    stewardBiggerCameraNodeId,
    "Make the subject bigger over time with editable camera controls.",
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{4.0}}
  );
  GRAPPLE_REQUIRE(stewardBiggerEffect);
  const agent::AgentConversationState stewardBiggerConversation =
    stewardMotionWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(stewardBiggerConversation.runs.size() == 3);
  GRAPPLE_REQUIRE(stewardBiggerConversation.runs[2].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardBiggerConversation.runs[2].toolCalls.size() == 3);
  GRAPPLE_REQUIRE(stewardBiggerConversation.runs[2].toolCalls[0].toolSerializedId == "effect.create_node");
  GRAPPLE_REQUIRE(stewardBiggerConversation.runs[2].toolCalls[1].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(stewardBiggerConversation.runs[2].toolCalls[2].toolSerializedId == "effect.create_param_keyframe");

  const auto stewardBiggerViewModel = stewardMotionWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(stewardBiggerViewModel);
  const app::AppEffectGraphRow* biggerEffectGraph = nullptr;
  for (const app::AppEffectGraphRow& graph : stewardBiggerViewModel.value().timeline.effectGraphs) {
    if (graph.targetNodeId == stewardBiggerCameraNodeId) {
      biggerEffectGraph = &graph;
    }
  }
  GRAPPLE_REQUIRE(biggerEffectGraph != nullptr);
  GRAPPLE_REQUIRE(biggerEffectGraph->effects.size() == 1);
  GRAPPLE_REQUIRE(biggerEffectGraph->effects[0].params[2].name == effects::builtin_effect::ZoomParam);
  GRAPPLE_REQUIRE(biggerEffectGraph->effects[0].params[2].keyframes.size() == 2);
  GRAPPLE_REQUIRE(biggerEffectGraph->effects[0].params[2].keyframes[0].time == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(std::get<double>(biggerEffectGraph->effects[0].params[2].keyframes[0].value) == 1.0);
  GRAPPLE_REQUIRE(biggerEffectGraph->effects[0].params[2].keyframes[1].time == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(std::get<double>(biggerEffectGraph->effects[0].params[2].keyframes[1].value) == 1.5);

  const auto stewardBiggerRefresh = stewardMotionWorkspace.value().preview().refreshFromProject();
  GRAPPLE_REQUIRE(stewardBiggerRefresh);
  const auto stewardBiggerMidFrame = stewardMotionWorkspace.value().preview().renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{2.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(stewardBiggerMidFrame);
  GRAPPLE_REQUIRE(stewardBiggerMidFrame.value().runtimeDiagnostics.empty());
  const render::RenderedCamera* biggerRenderedCamera = nullptr;
  for (const render::RenderedCamera& camera : stewardBiggerMidFrame.value().frame.cameras) {
    if (camera.cameraNodeId == stewardBiggerCameraNodeId) {
      biggerRenderedCamera = &camera;
    }
  }
  GRAPPLE_REQUIRE(biggerRenderedCamera != nullptr);
  GRAPPLE_REQUIRE(biggerRenderedCamera->state.transform.scale.x == 1.25);
  GRAPPLE_REQUIRE(biggerRenderedCamera->state.transform.scale.y == 1.25);

  runtime::RuntimeEvaluator appRuntime;
  render::LocalRenderCore appRenderCore{appRuntime};
  render::LocalRenderSystem appRenderSystem{appRenderCore};
  app::NativePreviewSession preview{session, appRenderSystem};
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
  GRAPPLE_REQUIRE(frame.value().frame.description == "layers=0 clips=0 textClips=0 audioClips=0 cameras=0 effects=0");
  GRAPPLE_REQUIRE(frame.value().frame.mediaFrames.empty());
  GRAPPLE_REQUIRE(frame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(frame.value().renderDiagnostics.empty());

  app::NativeExportSession exportSession{appRenderSystem};
  const auto exportPlan = session.buildRenderPlan();
  GRAPPLE_REQUIRE(exportPlan);
  const auto exportAfterPreviewRefresh = exportSession.renderPlan(exportPlan.value().plan, render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export.mov"}
  });
  GRAPPLE_REQUIRE(exportAfterPreviewRefresh);
  GRAPPLE_REQUIRE(exportAfterPreviewRefresh.value().sourceRevision == exportPlan.value().plan.revision);
  GRAPPLE_REQUIRE(exportAfterPreviewRefresh.value().renderPlanHash == preview.state().core.preparedPlanHash.value());
  GRAPPLE_REQUIRE(exportAfterPreviewRefresh.value().framesEvaluated == 2);

  GRAPPLE_REQUIRE(exportSession.state().core.hasPlan);
  const auto explicitPlanExport = exportSession.renderPlan(exportPlan.value().plan, render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-explicit-plan-export.mov"}
  });
  GRAPPLE_REQUIRE(explicitPlanExport);
  GRAPPLE_REQUIRE(explicitPlanExport.value().framesEvaluated == 2);
  const foundation::Hash256 explicitPlanHash = exportSession.state().core.preparedPlanHash.value();
  const auto projectBeforeExport = session.snapshot();
  GRAPPLE_REQUIRE(projectBeforeExport);
  GRAPPLE_REQUIRE(projectBeforeExport.value().revision == foundation::RevisionId{"rev_1"});
  const std::string serializedProjectBeforeExport =
    project::serializeCanonicalProjectSnapshot(projectBeforeExport.value());

  const auto exportResult = exportSession.renderPlan(exportPlan.value().plan, render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1920, 1080},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export.mov"}
  });
  GRAPPLE_REQUIRE(exportResult);
  GRAPPLE_REQUIRE(exportResult.value().outputPath.value == "/tmp/app-export.mov");
  GRAPPLE_REQUIRE(exportResult.value().sourceRevision == exportPlan.value().plan.revision);
  GRAPPLE_REQUIRE(exportResult.value().renderPlanHash == explicitPlanHash);
  GRAPPLE_REQUIRE(exportResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(exportResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(exportResult.value().renderDiagnostics.empty());
  GRAPPLE_REQUIRE(exportSession.state().lastOutputPath->value == "/tmp/app-export.mov");
  GRAPPLE_REQUIRE(exportSession.state().core.preparedPlanHash == explicitPlanHash);

  const auto changedExportResolution = exportSession.renderPlan(exportPlan.value().plan, render::ExportSettings{
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
    foundation::FrameRate{2, 1},
    foundation::Resolution{1280, 720},
    render::Codec{"mjpeg"},
    render::RenderQuality::Final,
    foundation::FilePath{"/tmp/app-export-720.mov"}
  });
  GRAPPLE_REQUIRE(changedExportResolution);
  GRAPPLE_REQUIRE(exportSession.state().core.preparedPlanHash == explicitPlanHash);
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

  const history::SnapshotRecord* saveAsHeadSnapshot =
    openedWorkspace.value().project().packageState().snapshots.findByRevision(foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(saveAsHeadSnapshot != nullptr);
  const std::string packageRootBeforeFailedSaveAs =
    openedWorkspace.value().project().packageState().package.rootPath.value;
  const auto failedSaveAs = openedWorkspace.value().savePackageAs(foundation::FilePath{});
  GRAPPLE_REQUIRE(!failedSaveAs);
  GRAPPLE_REQUIRE(openedWorkspace.value().project().packageState().package.rootPath.value == packageRootBeforeFailedSaveAs);
  const auto saveAsWrite = openedWorkspace.value().savePackageAs(foundation::FilePath{saveAsPackageRoot.string()});
  GRAPPLE_REQUIRE(saveAsWrite);
  GRAPPLE_REQUIRE(saveAsWrite.value().project.snapshotPath.value == (saveAsPackageRoot / saveAsHeadSnapshot->documentPath.value).lexically_normal().string());
  GRAPPLE_REQUIRE(saveAsWrite.value().project.manifestPath.value == (saveAsPackageRoot / "manifest.json").lexically_normal().string());
  GRAPPLE_REQUIRE(saveAsWrite.value().project.commandLogPath.value == (saveAsPackageRoot / "history/commands.json").lexically_normal().string());
  GRAPPLE_REQUIRE(saveAsWrite.value().project.eventLogPath.value == (saveAsPackageRoot / "history/events.json").lexically_normal().string());
  GRAPPLE_REQUIRE(saveAsWrite.value().agentRunsPath.value == (saveAsPackageRoot / "agent/runs.json").lexically_normal().string());
  GRAPPLE_REQUIRE(saveAsWrite.value().agentEventsPath.value == (saveAsPackageRoot / "agent/events.json").lexically_normal().string());
  GRAPPLE_REQUIRE(openedWorkspace.value().project().packageState().package.rootPath.value == saveAsPackageRoot.string());
  auto reopenedSaveAsWorkspace = app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{saveAsPackageRoot.string()});
  GRAPPLE_REQUIRE(reopenedSaveAsWorkspace);
  const auto reopenedSaveAsViewModel = reopenedSaveAsWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(reopenedSaveAsViewModel);
  GRAPPLE_REQUIRE(reopenedSaveAsViewModel.value().project.projectId == foundation::ProjectId{"proj_app_saved"});
  GRAPPLE_REQUIRE(reopenedSaveAsViewModel.value().project.revision == foundation::RevisionId{"rev_2"});
  GRAPPLE_REQUIRE(reopenedSaveAsViewModel.value().timeline.layers.size() == 1);
  GRAPPLE_REQUIRE(reopenedSaveAsWorkspace.value().steward().conversationState().diagnostics.empty());

  std::filesystem::remove_all(packageRoot);
  std::filesystem::remove_all(saveAsPackageRoot);

  app::NativeProjectSession projectOnlySession{
    foundation::ProjectId{"proj_app_project_only"},
    "Project Only App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_project_only"},
      foundation::FilePath{projectOnlyPackageRoot.string()},
      storage::CurrentProjectPackageSchemaVersion
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
      storage::CurrentProjectPackageSchemaVersion
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
      timeline::CameraPayload{
        "Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
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
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].toolCalls[0].toolSerializedId == "effect.create_node");
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].toolCalls[0].toolDisplayName == "Create Effect Node");
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_1"});
  GRAPPLE_REQUIRE(reopenedStewardConversation.runs[0].toolCalls[0].observedRevision == foundation::RevisionId{"rev_3"});
  const auto reopenedStewardViewModel = reopenedStewardWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(reopenedStewardViewModel);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().project.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].targetName == "Camera");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects[0].displayName == "Camera Transform");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects[0].createdRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects[0].createdSourceKind == "agent");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects[0].createdActorName == "steward");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().timeline.effectGraphs[0].effects[0].createdIntent == durableIntent);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].targetNodeId == stewardCameraNodeId);
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].targetName == "Camera");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].editName == "Camera Transform");
  GRAPPLE_REQUIRE(reopenedStewardViewModel.value().steward.edits[0].intent == durableIntent);
  const foundation::NodeId reopenedSecondCameraNodeId = reopenedStewardWorkspace.value().commandWriter().nextNodeId("camera");
  const auto reopenedSecondCamera = reopenedStewardWorkspace.value().commandWriter().apply(
    project::CreateCameraCommand{
      reopenedSecondCameraNodeId,
      stewardCompositionNodeId,
      reopenedStewardWorkspace.value().commandWriter().nextEdgeId("contains camera"),
      timeline::CameraPayload{
        "Second Camera",
        timeline::CameraState{
          timeline::Transform2D{},
          timeline::CameraLens{35.0}
        }
      }
    },
    userSource()
  );
  GRAPPLE_REQUIRE(reopenedSecondCamera);
  const std::string durableMotionIntent = "Pan right with editable camera controls.";
  const auto reopenedSecondStewardEffect = reopenedStewardWorkspace.value().steward().createCameraTransformEffect(
    reopenedSecondCameraNodeId,
    durableMotionIntent,
    foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{2.0}}
  );
  GRAPPLE_REQUIRE(reopenedSecondStewardEffect);
  const agent::AgentConversationState reopenedStewardConversationAfterSecondRun =
    reopenedStewardWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.diagnostics.empty());
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs.size() == 2);
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].runId == foundation::RunId{"run_steward_2"});
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls.size() == 3);
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_camera_transform_2"});
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_5"});
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls[1].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_2_1"});
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls[1].observedRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls[2].toolCallId == foundation::ToolId{"tool_steward_camera_transform_keyframe_2_2"});
  GRAPPLE_REQUIRE(reopenedStewardConversationAfterSecondRun.runs[1].toolCalls[2].observedRevision == reopenedSecondStewardEffect.value().snapshot.revision);
  const auto stewardMotionWrite = reopenedStewardWorkspace.value().writePackage();
  GRAPPLE_REQUIRE(stewardMotionWrite);
  auto reopenedStewardMotionWorkspace = app::NativeWorkspaceSession::openPackageRoot(foundation::FilePath{stewardPackageRoot.string()});
  GRAPPLE_REQUIRE(reopenedStewardMotionWorkspace);
  const agent::AgentConversationState durableMotionConversation =
    reopenedStewardMotionWorkspace.value().steward().conversationState();
  GRAPPLE_REQUIRE(durableMotionConversation.diagnostics.empty());
  GRAPPLE_REQUIRE(durableMotionConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(durableMotionConversation.runs[1].runId == foundation::RunId{"run_steward_2"});
  GRAPPLE_REQUIRE(durableMotionConversation.runs[1].title == durableMotionIntent);
  GRAPPLE_REQUIRE(durableMotionConversation.runs[1].toolCalls.size() == 3);
  GRAPPLE_REQUIRE(durableMotionConversation.runs[1].toolCalls[1].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(durableMotionConversation.runs[1].toolCalls[2].toolSerializedId == "effect.create_param_keyframe");
  GRAPPLE_REQUIRE(durableMotionConversation.runs[1].toolCalls[2].observedRevision == foundation::RevisionId{"rev_7"});
  const auto durableMotionViewModel = reopenedStewardMotionWorkspace.value().project().buildViewModel();
  GRAPPLE_REQUIRE(durableMotionViewModel);
  GRAPPLE_REQUIRE(durableMotionViewModel.value().project.revision == foundation::RevisionId{"rev_7"});
  const app::AppEffectGraphRow* durableMotionGraph = nullptr;
  for (const app::AppEffectGraphRow& graph : durableMotionViewModel.value().timeline.effectGraphs) {
    if (graph.targetNodeId == reopenedSecondCameraNodeId) {
      durableMotionGraph = &graph;
    }
  }
  GRAPPLE_REQUIRE(durableMotionGraph != nullptr);
  GRAPPLE_REQUIRE(durableMotionGraph->effects.size() == 1);
  GRAPPLE_REQUIRE(durableMotionGraph->effects[0].createdIntent == durableMotionIntent);
  GRAPPLE_REQUIRE(durableMotionGraph->effects[0].params[0].name == effects::builtin_effect::PositionXParam);
  GRAPPLE_REQUIRE(durableMotionGraph->effects[0].params[0].keyframes.size() == 2);
  GRAPPLE_REQUIRE(durableMotionGraph->effects[0].params[0].keyframes[0].time == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(std::get<double>(durableMotionGraph->effects[0].params[0].keyframes[0].value) == 0.0);
  GRAPPLE_REQUIRE(durableMotionGraph->effects[0].params[0].keyframes[0].lastEditedActorName == "steward");
  GRAPPLE_REQUIRE(durableMotionGraph->effects[0].params[0].keyframes[1].time == foundation::TimeSeconds{2.0});
  GRAPPLE_REQUIRE(std::get<double>(durableMotionGraph->effects[0].params[0].keyframes[1].value) == 0.25);
  GRAPPLE_REQUIRE(durableMotionGraph->effects[0].params[0].keyframes[1].lastEditedActorName == "steward");
  std::filesystem::remove_all(stewardPackageRoot);

  app::NativeProjectSession noteSession{
    foundation::ProjectId{"proj_app_notes"},
    "Notes App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_notes"},
      foundation::FilePath{"notes-app.grapple"},
      storage::CurrentProjectPackageSchemaVersion
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
  app::NativeStewardSession noteSteward{noteSession, noteWriter};
  auto stewardNote = noteSteward.createNote("Add note \"Edit rationale\" saying Keep the crop user-editable.");
  GRAPPLE_REQUIRE(stewardNote);
  GRAPPLE_REQUIRE(stewardNote.value().noteNodeId == foundation::NodeId{"node_note_2"});
  auto stewardNoteEdit = noteSteward.editNote(
    stewardNote.value().noteNodeId,
    "Update note to \"Keep the crop exposed as an editable control.\""
  );
  GRAPPLE_REQUIRE(stewardNoteEdit);
  const auto stewardNoteViewModel = noteSession.buildViewModel();
  GRAPPLE_REQUIRE(stewardNoteViewModel);
  GRAPPLE_REQUIRE(stewardNoteViewModel.value().notes.rows.size() == 2);
  GRAPPLE_REQUIRE(stewardNoteViewModel.value().notes.rows[1].sourceNodeId == foundation::NodeId{"node_note_2"});
  GRAPPLE_REQUIRE(stewardNoteViewModel.value().notes.rows[1].title == "Edit rationale");
  GRAPPLE_REQUIRE(stewardNoteViewModel.value().notes.rows[1].markdown == "Keep the crop exposed as an editable control.");
  GRAPPLE_REQUIRE(stewardNoteViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardNoteViewModel.value().steward.edits[0].editName == "Note");
  GRAPPLE_REQUIRE(stewardNoteViewModel.value().steward.edits[1].editName == "Note");
  const auto stewardNoteConversation = noteSteward.conversationState();
  GRAPPLE_REQUIRE(stewardNoteConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardNoteConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardNoteConversation.runs[0].toolCalls[0].toolSerializedId == "note.create");
  GRAPPLE_REQUIRE(stewardNoteConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardNoteConversation.runs[1].toolCalls[0].toolSerializedId == "note.update");
  auto stewardUndo = noteSteward.undoLastEdit("Undo last edit.");
  GRAPPLE_REQUIRE(stewardUndo);
  const auto stewardUndoViewModel = noteSession.buildViewModel();
  GRAPPLE_REQUIRE(stewardUndoViewModel);
  GRAPPLE_REQUIRE(stewardUndoViewModel.value().notes.rows.size() == 2);
  GRAPPLE_REQUIRE(stewardUndoViewModel.value().notes.rows[1].sourceNodeId == foundation::NodeId{"node_note_2"});
  GRAPPLE_REQUIRE(stewardUndoViewModel.value().notes.rows[1].title == "Edit rationale");
  GRAPPLE_REQUIRE(stewardUndoViewModel.value().notes.rows[1].markdown == "Keep the crop user-editable");
  GRAPPLE_REQUIRE(stewardUndoViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(stewardUndo.value().commandResult.beforeRevision == stewardNoteEdit.value().snapshot.revision);
  GRAPPLE_REQUIRE(stewardUndo.value().snapshot.revision == foundation::RevisionId{"rev_4"});
  const history::CommandRecord& undoCommand = noteSession.packageState().commandLog.records().back();
  GRAPPLE_REQUIRE(undoCommand.serializedName == "project.restore_snapshot");
  GRAPPLE_REQUIRE(undoCommand.sourceKind == "agent");
  GRAPPLE_REQUIRE(undoCommand.sourceActorName == "steward");
  GRAPPLE_REQUIRE(undoCommand.sourceRunId == foundation::RunId{"run_steward_3"});
  const auto stewardUndoConversation = noteSteward.conversationState();
  GRAPPLE_REQUIRE(stewardUndoConversation.runs.size() == 3);
  GRAPPLE_REQUIRE(stewardUndoConversation.runs[2].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardUndoConversation.runs[2].toolCalls.empty());
  GRAPPLE_REQUIRE(stewardUndoConversation.runs[2].messages.size() == 1);
  GRAPPLE_REQUIRE(stewardUndoConversation.runs[2].messages[0].content == "Undoing the last committed project edit.");
  auto stewardRedo = noteSteward.redoLastEdit("Redo last edit.");
  GRAPPLE_REQUIRE(stewardRedo);
  const auto stewardRedoViewModel = noteSession.buildViewModel();
  GRAPPLE_REQUIRE(stewardRedoViewModel);
  GRAPPLE_REQUIRE(stewardRedoViewModel.value().notes.rows[1].sourceNodeId == foundation::NodeId{"node_note_2"});
  GRAPPLE_REQUIRE(stewardRedoViewModel.value().notes.rows[1].title == "Edit rationale");
  GRAPPLE_REQUIRE(stewardRedoViewModel.value().notes.rows[1].markdown == "Keep the crop exposed as an editable control.");
  GRAPPLE_REQUIRE(stewardRedoViewModel.value().steward.edits.size() == 3);
  GRAPPLE_REQUIRE(stewardRedoViewModel.value().steward.edits[2].editName == "Note");
  GRAPPLE_REQUIRE(stewardRedoViewModel.value().steward.edits[2].intent == "Redo last edit.");
  GRAPPLE_REQUIRE(stewardRedo.value().commandResult.beforeRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(stewardRedo.value().snapshot.revision == foundation::RevisionId{"rev_5"});
  const history::CommandRecord& redoCommand = noteSession.packageState().commandLog.records().back();
  GRAPPLE_REQUIRE(redoCommand.serializedName == "project.update_note");
  GRAPPLE_REQUIRE(redoCommand.sourceKind == "agent");
  GRAPPLE_REQUIRE(redoCommand.sourceActorName == "steward");
  GRAPPLE_REQUIRE(redoCommand.sourceRunId == foundation::RunId{"run_steward_4"});
  const auto stewardRedoConversation = noteSteward.conversationState();
  GRAPPLE_REQUIRE(stewardRedoConversation.runs.size() == 4);
  GRAPPLE_REQUIRE(stewardRedoConversation.runs[3].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardRedoConversation.runs[3].toolCalls.empty());
  GRAPPLE_REQUIRE(stewardRedoConversation.runs[3].messages.size() == 1);
  GRAPPLE_REQUIRE(stewardRedoConversation.runs[3].messages[0].content == "Redoing the last undone project edit.");

  app::NativeProjectSession trackDeleteSession{
    foundation::ProjectId{"proj_app_steward_track_delete"},
    "Steward Track Delete Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_track_delete"},
      foundation::FilePath{"steward-track-delete.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  app::NativeProjectCommandWriter trackDeleteWriter{trackDeleteSession};
  const foundation::NodeId trackDeleteCompositionNodeId = trackDeleteWriter.nextNodeId("composition");
  const auto trackDeleteComposition = trackDeleteWriter.apply(
    project::CreateCompositionCommand{trackDeleteCompositionNodeId, "Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(trackDeleteComposition);
  const foundation::NodeId trackDeleteTrackNodeId = trackDeleteWriter.nextNodeId("track");
  const auto trackDeleteTrack = trackDeleteWriter.apply(
    project::CreateTrackCommand{
      trackDeleteTrackNodeId,
      trackDeleteCompositionNodeId,
      trackDeleteWriter.nextEdgeId("contains_track"),
      "Video 1",
      timeline::TrackKind::Visual
    },
    userSource()
  );
  GRAPPLE_REQUIRE(trackDeleteTrack);
  app::NativeStewardSession trackDeleteSteward{trackDeleteSession, trackDeleteWriter};
  auto stewardTrackDelete = trackDeleteSteward.deleteTrack(trackDeleteTrackNodeId, "Delete selected track.");
  GRAPPLE_REQUIRE(stewardTrackDelete);
  GRAPPLE_REQUIRE(stewardTrackDelete.value().snapshot.revision == foundation::RevisionId{"rev_3"});
  const auto stewardTrackDeleteViewModel = trackDeleteSession.buildViewModel();
  GRAPPLE_REQUIRE(stewardTrackDeleteViewModel);
  GRAPPLE_REQUIRE(stewardTrackDeleteViewModel.value().timeline.layers.empty());
  GRAPPLE_REQUIRE(stewardTrackDeleteViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(stewardTrackDeleteViewModel.value().steward.edits[0].editName == "Track Delete");
  GRAPPLE_REQUIRE(stewardTrackDeleteViewModel.value().steward.edits[0].targetName == "Video 1");
  GRAPPLE_REQUIRE(stewardTrackDeleteViewModel.value().steward.edits[0].controlSummary == "Deleted");
  const history::CommandRecord& trackDeleteCommand = trackDeleteSession.packageState().commandLog.records().back();
  GRAPPLE_REQUIRE(trackDeleteCommand.serializedName == "project.delete_track");
  GRAPPLE_REQUIRE(trackDeleteCommand.sourceKind == "agent");
  GRAPPLE_REQUIRE(trackDeleteCommand.sourceActorName == "steward");
  GRAPPLE_REQUIRE(trackDeleteCommand.sourceRunId == foundation::RunId{"run_steward_1"});
  const auto stewardTrackDeleteConversation = trackDeleteSteward.conversationState();
  GRAPPLE_REQUIRE(stewardTrackDeleteConversation.runs.size() == 1);
  GRAPPLE_REQUIRE(stewardTrackDeleteConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardTrackDeleteConversation.runs[0].toolCalls[0].toolSerializedId == "timeline.delete_track");

  app::NativeProjectSession trackCreateSession{
    foundation::ProjectId{"proj_app_steward_track_create"},
    "Steward Track Create",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_track_create"},
      foundation::FilePath{"steward-track-create.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  app::NativeProjectCommandWriter trackCreateWriter{trackCreateSession};
  const foundation::NodeId trackCreateCompositionNodeId = trackCreateWriter.nextNodeId("composition");
  const auto trackCreateComposition = trackCreateWriter.apply(
    project::CreateCompositionCommand{trackCreateCompositionNodeId, "Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(trackCreateComposition);
  app::NativeStewardSession trackCreateSteward{trackCreateSession, trackCreateWriter};
  auto stewardTrackCreate = trackCreateSteward.createTrack("Create audio layer.");
  GRAPPLE_REQUIRE(stewardTrackCreate);
  GRAPPLE_REQUIRE(stewardTrackCreate.value().trackNodeId == foundation::NodeId{"node_track_2"});
  GRAPPLE_REQUIRE(stewardTrackCreate.value().packageResult.snapshot.revision == foundation::RevisionId{"rev_2"});
  const auto stewardTrackCreateViewModel = trackCreateSession.buildViewModel();
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel);
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().timeline.layers.empty());
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().timeline.audioTracks.size() == 1);
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().timeline.audioTracks[0].sourceNodeId == stewardTrackCreate.value().trackNodeId);
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().timeline.audioTracks[0].name == "Audio Track");
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().steward.edits[0].editName == "Track");
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().steward.edits[0].targetName == "Audio Track");
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().steward.edits[0].intent == "Create audio layer.");
  GRAPPLE_REQUIRE(stewardTrackCreateViewModel.value().steward.edits[0].controlSummary == "Kind=audio");
  const history::CommandRecord& trackCreateCommand = trackCreateSession.packageState().commandLog.records().back();
  GRAPPLE_REQUIRE(trackCreateCommand.serializedName == "project.create_track");
  GRAPPLE_REQUIRE(trackCreateCommand.sourceKind == "agent");
  GRAPPLE_REQUIRE(trackCreateCommand.sourceActorName == "steward");
  GRAPPLE_REQUIRE(trackCreateCommand.sourceRunId == foundation::RunId{"run_steward_1"});
  const auto stewardTrackCreateConversation = trackCreateSteward.conversationState();
  GRAPPLE_REQUIRE(stewardTrackCreateConversation.runs.size() == 1);
  GRAPPLE_REQUIRE(stewardTrackCreateConversation.runs[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardTrackCreateConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardTrackCreateConversation.runs[0].toolCalls[0].toolSerializedId == "timeline.create_track");
  GRAPPLE_REQUIRE(stewardTrackCreateConversation.runs[0].toolCalls[0].toolDisplayName == "Create Timeline Track");
  GRAPPLE_REQUIRE(stewardTrackCreateConversation.runs[0].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_create_track_1"});
  GRAPPLE_REQUIRE(stewardTrackCreateConversation.runs[0].toolCalls[0].observedRevision == foundation::RevisionId{"rev_2"});

  app::NativeProjectSession cameraCreateSession{
    foundation::ProjectId{"proj_app_steward_camera_create"},
    "Steward Camera Create",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app_steward_camera_create"},
      foundation::FilePath{"steward-camera-create.grapple"},
      storage::CurrentProjectPackageSchemaVersion
    }
  };
  app::NativeProjectCommandWriter cameraCreateWriter{cameraCreateSession};
  const foundation::NodeId cameraCreateCompositionNodeId = cameraCreateWriter.nextNodeId("composition");
  const auto cameraCreateComposition = cameraCreateWriter.apply(
    project::CreateCompositionCommand{cameraCreateCompositionNodeId, "Main"},
    userSource()
  );
  GRAPPLE_REQUIRE(cameraCreateComposition);
  app::NativeStewardSession cameraCreateSteward{cameraCreateSession, cameraCreateWriter};
  auto stewardCameraCreate = cameraCreateSteward.createCamera();
  GRAPPLE_REQUIRE(stewardCameraCreate);
  GRAPPLE_REQUIRE(stewardCameraCreate.value().cameraNodeId == foundation::NodeId{"node_camera_2"});
  GRAPPLE_REQUIRE(stewardCameraCreate.value().packageResult.snapshot.revision == foundation::RevisionId{"rev_2"});
  const auto stewardCameraCreateViewModel = cameraCreateSession.buildViewModel();
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel);
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().timeline.cameras.size() == 1);
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().timeline.cameras[0].sourceNodeId == stewardCameraCreate.value().cameraNodeId);
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().timeline.cameras[0].name == "Camera 1");
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().timeline.cameras[0].state.lens.focalLength == 35.0);
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().steward.edits.size() == 1);
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().steward.edits[0].editName == "Camera");
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().steward.edits[0].targetName == "Camera 1");
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().steward.edits[0].intent == "Add camera.");
  GRAPPLE_REQUIRE(stewardCameraCreateViewModel.value().steward.edits[0].controlSummary == "Focal Length=35");
  const history::CommandRecord& cameraCreateCommand = cameraCreateSession.packageState().commandLog.records().back();
  GRAPPLE_REQUIRE(cameraCreateCommand.serializedName == "project.create_camera");
  GRAPPLE_REQUIRE(cameraCreateCommand.sourceKind == "agent");
  GRAPPLE_REQUIRE(cameraCreateCommand.sourceActorName == "steward");
  GRAPPLE_REQUIRE(cameraCreateCommand.sourceRunId == foundation::RunId{"run_steward_1"});
  const auto stewardCameraCreateConversation = cameraCreateSteward.conversationState();
  GRAPPLE_REQUIRE(stewardCameraCreateConversation.runs.size() == 1);
  GRAPPLE_REQUIRE(stewardCameraCreateConversation.runs[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardCameraCreateConversation.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardCameraCreateConversation.runs[0].toolCalls[0].toolSerializedId == "camera.create");
  GRAPPLE_REQUIRE(stewardCameraCreateConversation.runs[0].toolCalls[0].toolDisplayName == "Create Camera");
  GRAPPLE_REQUIRE(stewardCameraCreateConversation.runs[0].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_create_camera_1"});
  GRAPPLE_REQUIRE(stewardCameraCreateConversation.runs[0].toolCalls[0].observedRevision == foundation::RevisionId{"rev_2"});
  const auto stewardCameraUpdate = cameraCreateSteward.updateCamera(
    stewardCameraCreate.value().cameraNodeId,
    "Rename camera to \"Closeup\" and set camera focal length to 50."
  );
  GRAPPLE_REQUIRE(stewardCameraUpdate);
  GRAPPLE_REQUIRE(stewardCameraUpdate.value().snapshot.revision == foundation::RevisionId{"rev_3"});
  const auto stewardCameraUpdateViewModel = cameraCreateSession.buildViewModel();
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel);
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel.value().timeline.cameras.size() == 1);
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel.value().timeline.cameras[0].name == "Closeup");
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel.value().timeline.cameras[0].state.lens.focalLength == 50.0);
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel.value().steward.edits.size() == 2);
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel.value().steward.edits[1].editName == "Camera");
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel.value().steward.edits[1].targetName == "Closeup");
  GRAPPLE_REQUIRE(
    stewardCameraUpdateViewModel.value().steward.edits[1].intent ==
    "Rename camera to \"Closeup\" and set camera focal length to 50."
  );
  GRAPPLE_REQUIRE(stewardCameraUpdateViewModel.value().steward.edits[1].controlSummary == "Focal Length=50");
  const history::CommandRecord& cameraUpdateCommand = cameraCreateSession.packageState().commandLog.records().back();
  GRAPPLE_REQUIRE(cameraUpdateCommand.serializedName == "project.update_camera");
  GRAPPLE_REQUIRE(cameraUpdateCommand.sourceKind == "agent");
  GRAPPLE_REQUIRE(cameraUpdateCommand.sourceActorName == "steward");
  GRAPPLE_REQUIRE(cameraUpdateCommand.sourceRunId == foundation::RunId{"run_steward_2"});
  const auto stewardCameraUpdateConversation = cameraCreateSteward.conversationState();
  GRAPPLE_REQUIRE(stewardCameraUpdateConversation.runs.size() == 2);
  GRAPPLE_REQUIRE(stewardCameraUpdateConversation.runs[1].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(stewardCameraUpdateConversation.runs[1].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(stewardCameraUpdateConversation.runs[1].toolCalls[0].toolSerializedId == "camera.update");
  GRAPPLE_REQUIRE(stewardCameraUpdateConversation.runs[1].toolCalls[0].toolDisplayName == "Update Camera");
  GRAPPLE_REQUIRE(stewardCameraUpdateConversation.runs[1].toolCalls[0].toolCallId == foundation::ToolId{"tool_steward_update_camera_2"});
  GRAPPLE_REQUIRE(stewardCameraUpdateConversation.runs[1].toolCalls[0].observedRevision == foundation::RevisionId{"rev_3"});

  const std::string workspacePackageStem =
    "grapple_workspace_package_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path workspacePackageRoot = std::filesystem::temp_directory_path() / workspacePackageStem;
  std::filesystem::remove_all(workspacePackageRoot);
  auto workspacePackage = app::NativeWorkspaceSession::createPackageRoot(
    foundation::FilePath{workspacePackageRoot.string()},
    "Workspace Package"
  );
  GRAPPLE_REQUIRE(workspacePackage);
  const std::filesystem::path workspaceImportSource = writeTinyPpm("grapple_workspace_package_import");
  const auto workspaceImportedAssetId = workspacePackage.value().importMediaFile(
    foundation::FilePath{workspaceImportSource.string()}
  );
  GRAPPLE_REQUIRE(workspaceImportedAssetId);
  const auto workspacePackageAfterImport = workspacePackage.value().project().buildViewModel();
  GRAPPLE_REQUIRE(workspacePackageAfterImport);
  GRAPPLE_REQUIRE(workspacePackageAfterImport.value().assets.count == 1);
  const foundation::FilePath workspaceImportedSourcePath =
    workspacePackageAfterImport.value().assets.rows[0].sourcePath;
  const std::optional<foundation::FilePath> workspaceImportedThumbnailPath =
    workspacePackageAfterImport.value().assets.rows[0].thumbnailPath;
  GRAPPLE_REQUIRE(workspaceImportedSourcePath == foundation::FilePath{"assets/originals/" + workspaceImportedAssetId.value().value() + ".ppm"});
  GRAPPLE_REQUIRE(workspaceImportedThumbnailPath.has_value());
  GRAPPLE_REQUIRE(workspaceImportedThumbnailPath.value() == foundation::FilePath{"assets/thumbnails/" + workspaceImportedAssetId.value().value() + ".jpg"});
  GRAPPLE_REQUIRE(std::filesystem::exists(workspacePackageRoot / workspaceImportedSourcePath.value));
  GRAPPLE_REQUIRE(std::filesystem::exists(workspacePackageRoot / workspaceImportedThumbnailPath->value));
  GRAPPLE_REQUIRE(workspacePackage.value().mediaSources().sources().size() == 1);
  GRAPPLE_REQUIRE(workspacePackage.value().mediaSources().sources()[0].path == foundation::FilePath{(workspacePackageRoot / workspaceImportedSourcePath.value).lexically_normal().string()});
  const auto workspaceWrite = workspacePackage.value().writePackage();
  GRAPPLE_REQUIRE(workspaceWrite);
  std::filesystem::remove(workspaceImportSource);
  auto reopenedWorkspacePackage = app::NativeWorkspaceSession::openPackageRoot(
    foundation::FilePath{workspacePackageRoot.string()}
  );
  GRAPPLE_REQUIRE(reopenedWorkspacePackage);
  const auto workspacePackageViewModel = reopenedWorkspacePackage.value().project().buildViewModel();
  GRAPPLE_REQUIRE(workspacePackageViewModel);
  GRAPPLE_REQUIRE(workspacePackageViewModel.value().project.projectId == foundation::ProjectId{"proj_" + workspacePackageStem});
  GRAPPLE_REQUIRE(workspacePackageViewModel.value().project.name == "Workspace Package");
  GRAPPLE_REQUIRE(workspacePackageViewModel.value().project.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(workspacePackageViewModel.value().assets.count == 1);
  GRAPPLE_REQUIRE(workspacePackageViewModel.value().assets.rows[0].sourcePath == workspaceImportedSourcePath);
  GRAPPLE_REQUIRE(workspacePackageViewModel.value().assets.rows[0].thumbnailPath == workspaceImportedThumbnailPath);
  GRAPPLE_REQUIRE(std::filesystem::exists(workspacePackageRoot / workspacePackageViewModel.value().assets.rows[0].thumbnailPath->value));
  GRAPPLE_REQUIRE(workspacePackageViewModel.value().timeline.clips.empty());
  GRAPPLE_REQUIRE(reopenedWorkspacePackage.value().project().packageState().commandLog.records().size() == 1);
  GRAPPLE_REQUIRE(reopenedWorkspacePackage.value().project().packageState().snapshots.records().size() == 2);
  GRAPPLE_REQUIRE(reopenedWorkspacePackage.value().mediaSources().sources().size() == 1);
  GRAPPLE_REQUIRE(reopenedWorkspacePackage.value().mediaSources().sources()[0].path == foundation::FilePath{(workspacePackageRoot / workspaceImportedSourcePath.value).lexically_normal().string()});
  auto duplicateWorkspacePackage = app::NativeWorkspaceSession::createPackageRoot(
    foundation::FilePath{workspacePackageRoot.string()},
    "Duplicate Workspace Package"
  );
  GRAPPLE_REQUIRE(!duplicateWorkspacePackage);
  GRAPPLE_REQUIRE(duplicateWorkspacePackage.error().code == "app.package_manifest_already_exists");
  std::filesystem::remove_all(workspacePackageRoot);

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
