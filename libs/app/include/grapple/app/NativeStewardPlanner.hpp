#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <optional>
#include <string>
#include <vector>

namespace grapple::app {

struct CameraTransformIntentDefaults {
  double positionX = 0.0;
  double positionY = 0.0;
  double zoom = 1.0;
};

struct CameraTransformMotionKeyframes {
  std::string paramName;
  double startValue = 0.0;
  double endValue = 0.0;
  foundation::TimeSeconds endTime;
};

enum class CameraTransformAdjustmentOperation {
  Set,
  Add,
  Multiply
};

struct CameraTransformParamAdjustment {
  foundation::NodeId effectNodeId;
  std::string paramName;
  double value = 0.0;
  double operand = 0.0;
  CameraTransformAdjustmentOperation operation = CameraTransformAdjustmentOperation::Set;
};

struct CameraTransformKeyframeAdjustment {
  std::optional<foundation::KeyframeId> keyframeId;
  foundation::TimeSeconds time;
  double value = 0.0;
};

struct ClipEditIntent {
  timeline::Transform2D transform;
  double playbackRate = 1.0;
  std::optional<foundation::TimeSeconds> newStart;
  std::optional<foundation::TimeRange> timelineRange;
  std::optional<foundation::TimeRange> sourceRange;
  bool moveChanged = false;
  bool trimChanged = false;
  bool transformChanged = false;
  bool playbackRateChanged = false;
};

struct TextClipIntentDefaults {
  std::string text;
  foundation::TimeSeconds duration{3.0};
  timeline::Transform2D transform;
  timeline::TextClipStyle style;
};

struct TextClipEditIntent {
  timeline::TextClipPayload payload;
  bool changed = false;
};

struct NoteIntentDefaults {
  std::string title;
  std::string markdown;
};

struct NoteEditIntent {
  timeline::NotePayload payload;
  bool changed = false;
};

class NativeStewardPlanner final {
public:
  [[nodiscard]] CameraTransformIntentDefaults cameraTransformDefaultsForIntent(
    const std::string& intent
  ) const;
  [[nodiscard]] std::optional<CameraTransformMotionKeyframes> cameraMotionKeyframesForIntent(
    const std::string& intent,
    foundation::TimeRange activeRange
  ) const;
  [[nodiscard]] bool cameraIntentRequestsExplicitMotion(const std::string& intent) const;
  [[nodiscard]] bool clipEditIntentTargetsClip(const std::string& intent) const;
  [[nodiscard]] bool textClipIntentTargetsText(const std::string& intent) const;
  [[nodiscard]] TextClipIntentDefaults textClipDefaultsForIntent(const std::string& intent) const;
  [[nodiscard]] bool textClipEditIntentTargetsTextClip(const std::string& intent) const;
  [[nodiscard]] foundation::Result<TextClipEditIntent> textClipEditForIntent(
    const timeline::TextClipPayload& current,
    const std::string& intent
  ) const;
  [[nodiscard]] bool noteIntentTargetsNote(const std::string& intent) const;
  [[nodiscard]] NoteIntentDefaults noteDefaultsForIntent(const std::string& intent) const;
  [[nodiscard]] bool noteEditIntentTargetsNote(const std::string& intent) const;
  [[nodiscard]] foundation::Result<NoteEditIntent> noteEditForIntent(
    const timeline::NotePayload& current,
    const std::string& intent
  ) const;
  [[nodiscard]] foundation::Result<ClipEditIntent> clipEditForIntent(
    const timeline::ClipPayload& current,
    const std::string& intent
  ) const;
  [[nodiscard]] const timeline::EffectPayload* cameraTransformEffectPayload(
    const project::ProjectSnapshot& snapshot,
    const foundation::NodeId& cameraNodeId,
    foundation::NodeId& effectNodeId
  ) const;
  [[nodiscard]] foundation::Result<std::optional<foundation::KeyframeId>> effectParamKeyframeIdAtTime(
    const timeline::EffectPayload& payload,
    const std::string& paramName,
    foundation::TimeSeconds time
  ) const;
  [[nodiscard]] foundation::Result<std::vector<CameraTransformParamAdjustment>> cameraTransformParamAdjustmentsForIntent(
    const project::ProjectSnapshot& snapshot,
    const foundation::NodeId& cameraNodeId,
    const std::string& intent
  ) const;
  [[nodiscard]] foundation::Result<std::vector<CameraTransformKeyframeAdjustment>> adjustedCameraTransformKeyframes(
    const project::ProjectSnapshot& snapshot,
    const CameraTransformParamAdjustment& adjustment
  ) const;
  [[nodiscard]] foundation::Result<CameraTransformMotionKeyframes> cameraTransformMotionAdjustmentForIntent(
    const project::ProjectSnapshot& snapshot,
    const foundation::NodeId& cameraNodeId,
    const std::string& intent,
    foundation::TimeRange activeRange
  ) const;
};

} // namespace grapple::app
