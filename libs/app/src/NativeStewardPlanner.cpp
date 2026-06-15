#include <grapple/app/NativeStewardPlanner.hpp>

#include <grapple/effects/BuiltinEffects.hpp>
#include <grapple/graph/GraphNode.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <variant>

namespace grapple::app {

namespace {

constexpr double CenteredCameraTransformPositionX = 0.0;
constexpr double CenteredCameraTransformPositionY = 0.0;
constexpr double NormalCameraTransformZoom = 1.0;
constexpr double CameraTransformPositionXStep = 0.25;
constexpr double CameraTransformPositionYStep = 0.2;
constexpr double CameraTransformZoomInStep = 0.25;
constexpr double CameraTransformZoomOutStep = 0.2;

std::string lowercaseAscii(std::string value) {
  for (char& character : value) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

bool containsText(const std::string& value, std::string_view text) {
  return value.find(text) != std::string::npos;
}

bool containsAsciiWord(const std::string& value, std::string_view word) {
  std::size_t position = value.find(word);
  while (position != std::string::npos) {
    const bool leftBoundary =
      position == 0 ||
      std::isalnum(static_cast<unsigned char>(value[position - 1])) == 0;
    const std::size_t right = position + word.size();
    const bool rightBoundary =
      right >= value.size() ||
      std::isalnum(static_cast<unsigned char>(value[right])) == 0;
    if (leftBoundary && rightBoundary) {
      return true;
    }
    position = value.find(word, position + 1);
  }
  return false;
}

bool cameraIntentRequestsZoomOut(const std::string& normalized) {
  return containsText(normalized, "zoom out") ||
         containsAsciiWord(normalized, "wide") ||
         containsAsciiWord(normalized, "wider") ||
         containsAsciiWord(normalized, "smaller") ||
         containsAsciiWord(normalized, "shrink");
}

bool cameraIntentRequestsZoomIn(const std::string& normalized) {
  return containsText(normalized, "zoom in") ||
         containsAsciiWord(normalized, "closer") ||
         containsAsciiWord(normalized, "close") ||
         containsAsciiWord(normalized, "larger") ||
         containsAsciiWord(normalized, "bigger");
}

bool cameraIntentRequestsTemporalMotion(const std::string& normalized) {
  return containsAsciiWord(normalized, "pan") ||
         containsText(normalized, "animate") ||
         containsText(normalized, "over time") ||
         containsText(normalized, "gradual") ||
         containsText(normalized, "slowly");
}

bool cameraIntentRequestsCenter(const std::string& normalized) {
  return containsAsciiWord(normalized, "center") ||
         containsAsciiWord(normalized, "centre") ||
         containsAsciiWord(normalized, "recenter") ||
         containsAsciiWord(normalized, "recentre");
}

bool cameraIntentRequestsReset(const std::string& normalized) {
  return containsAsciiWord(normalized, "reset");
}

double intentStrengthMultiplier(const std::string& normalized) {
  if (containsText(normalized, "a little") ||
      containsAsciiWord(normalized, "slight") ||
      containsAsciiWord(normalized, "slightly") ||
      containsAsciiWord(normalized, "subtle") ||
      containsAsciiWord(normalized, "gently")) {
    return 0.5;
  }
  if (containsText(normalized, "a lot") ||
      containsAsciiWord(normalized, "much") ||
      containsAsciiWord(normalized, "far") ||
      containsAsciiWord(normalized, "dramatic") ||
      containsAsciiWord(normalized, "dramatically")) {
    return 2.0;
  }
  return 1.0;
}

foundation::Result<double> numericEffectParamValue(
  const timeline::EffectPayload& payload,
  const std::string& paramName
) {
  const auto param = std::find_if(payload.params.values.begin(), payload.params.values.end(), [&](const timeline::Param& value) {
    return value.name == paramName;
  });
  if (param == payload.params.values.end()) {
    return foundation::Error{
      "steward.camera_transform_param_missing",
      "Camera Transform controls are missing the requested parameter."
    };
  }
  const auto* value = std::get_if<double>(&param->value);
  if (value == nullptr) {
    return foundation::Error{
      "steward.camera_transform_param_not_numeric",
      "Camera Transform control parameter must be numeric."
    };
  }
  return *value;
}

double applyCameraTransformOperation(
  double currentValue,
  CameraTransformAdjustmentOperation operation,
  double operand
) {
  switch (operation) {
    case CameraTransformAdjustmentOperation::Set:
      return operand;
    case CameraTransformAdjustmentOperation::Add:
      return currentValue + operand;
    case CameraTransformAdjustmentOperation::Multiply:
      return currentValue * operand;
  }

  return currentValue;
}

foundation::Result<std::optional<CameraTransformParamAdjustment>> cameraTransformParamAdjustment(
  const timeline::EffectPayload& payload,
  const foundation::NodeId& effectNodeId,
  std::string paramName,
  CameraTransformAdjustmentOperation operation,
  double operand
) {
  auto currentValue = numericEffectParamValue(payload, paramName);
  if (!currentValue) {
    return currentValue.error();
  }

  const double value = applyCameraTransformOperation(currentValue.value(), operation, operand);
  if (value == currentValue.value()) {
    return std::optional<CameraTransformParamAdjustment>{};
  }

  return std::optional<CameraTransformParamAdjustment>{CameraTransformParamAdjustment{
    effectNodeId,
    std::move(paramName),
    value,
    operand,
    operation
  }};
}

} // namespace

CameraTransformIntentDefaults NativeStewardPlanner::cameraTransformDefaultsForIntent(
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  const double strength = intentStrengthMultiplier(normalized);
  CameraTransformIntentDefaults defaults;

  if (containsAsciiWord(normalized, "left")) {
    defaults.positionX = -CameraTransformPositionXStep * strength;
  } else if (containsAsciiWord(normalized, "right")) {
    defaults.positionX = CameraTransformPositionXStep * strength;
  }

  if (containsAsciiWord(normalized, "up")) {
    defaults.positionY = -CameraTransformPositionYStep * strength;
  } else if (containsAsciiWord(normalized, "down")) {
    defaults.positionY = CameraTransformPositionYStep * strength;
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    defaults.zoom = NormalCameraTransformZoom - CameraTransformZoomOutStep * strength;
  } else if (cameraIntentRequestsZoomIn(normalized)) {
    defaults.zoom = NormalCameraTransformZoom + 0.5 * strength;
  } else if (containsText(normalized, "subject")) {
    defaults.zoom = 1.1;
  }

  return defaults;
}

std::optional<CameraTransformMotionKeyframes> NativeStewardPlanner::cameraMotionKeyframesForIntent(
  const std::string& intent,
  foundation::TimeRange activeRange
) const {
  if (activeRange.end.value <= activeRange.start.value) {
    return std::nullopt;
  }

  const std::string normalized = lowercaseAscii(intent);
  const double strength = intentStrengthMultiplier(normalized);
  if (!cameraIntentRequestsTemporalMotion(normalized)) {
    return std::nullopt;
  }

  if (containsAsciiWord(normalized, "left")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionXParam,
      CenteredCameraTransformPositionX,
      -CameraTransformPositionXStep * strength,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "right")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionXParam,
      CenteredCameraTransformPositionX,
      CameraTransformPositionXStep * strength,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "up")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionYParam,
      CenteredCameraTransformPositionY,
      -CameraTransformPositionYStep * strength,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "down")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionYParam,
      CenteredCameraTransformPositionY,
      CameraTransformPositionYStep * strength,
      activeRange.end
    };
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::ZoomParam,
      NormalCameraTransformZoom,
      NormalCameraTransformZoom - CameraTransformZoomOutStep * strength,
      activeRange.end
    };
  }
  if (containsText(normalized, "zoom") || cameraIntentRequestsZoomIn(normalized)) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::ZoomParam,
      NormalCameraTransformZoom,
      NormalCameraTransformZoom + 0.5 * strength,
      activeRange.end
    };
  }

  return std::nullopt;
}

bool NativeStewardPlanner::cameraIntentRequestsExplicitMotion(const std::string& intent) const {
  return cameraIntentRequestsTemporalMotion(lowercaseAscii(intent));
}

foundation::Result<timeline::Transform2D> NativeStewardPlanner::clipTransformForIntent(
  const timeline::Transform2D& current,
  const std::string& intent
) const {
  const std::string normalized = lowercaseAscii(intent);
  const double strength = intentStrengthMultiplier(normalized);
  timeline::Transform2D transform = current;
  bool changed = false;

  if (containsAsciiWord(normalized, "left")) {
    transform.position.x -= CameraTransformPositionXStep * strength;
    changed = true;
  } else if (containsAsciiWord(normalized, "right")) {
    transform.position.x += CameraTransformPositionXStep * strength;
    changed = true;
  }

  if (containsAsciiWord(normalized, "up")) {
    transform.position.y -= CameraTransformPositionYStep * strength;
    changed = true;
  } else if (containsAsciiWord(normalized, "down")) {
    transform.position.y += CameraTransformPositionYStep * strength;
    changed = true;
  }

  if (containsText(normalized, "scale down") ||
      containsAsciiWord(normalized, "smaller") ||
      containsAsciiWord(normalized, "shrink")) {
    transform.scale.x *= 1.0 - CameraTransformZoomInStep * strength;
    transform.scale.y *= 1.0 - CameraTransformZoomInStep * strength;
    changed = true;
  } else if (containsText(normalized, "scale up") ||
             containsAsciiWord(normalized, "larger") ||
             containsAsciiWord(normalized, "bigger")) {
    transform.scale.x *= 1.0 + CameraTransformZoomInStep * strength;
    transform.scale.y *= 1.0 + CameraTransformZoomInStep * strength;
    changed = true;
  }

  if (containsAsciiWord(normalized, "fade") ||
      containsAsciiWord(normalized, "transparent") ||
      containsText(normalized, "half opacity")) {
    transform.opacity = 0.5;
    changed = true;
  }

  if (!changed) {
    return foundation::Error{
      "steward.clip_transform_intent_unknown",
      "Clip transform requests must explicitly mention movement, scale, or opacity."
    };
  }

  return transform;
}

const timeline::EffectPayload* NativeStewardPlanner::cameraTransformEffectPayload(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  foundation::NodeId& effectNodeId
) const {
  for (const graph::GraphEdge& edge : snapshot.graph.edges()) {
    if (!edge.enabled ||
        edge.kind != graph::EdgeKind::Targets ||
        edge.targetNodeId != cameraNodeId) {
      continue;
    }
    const graph::GraphNode* effectNode = snapshot.graph.findNode(edge.sourceNodeId);
    if (effectNode == nullptr || effectNode->kind != graph::NodeKind::Effect) {
      continue;
    }
    const auto* payload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
    if (payload == nullptr ||
        payload->displayName != effects::builtin_effect::CameraTransformDisplayName ||
        payload->implementation.kind != timeline::EffectImplementationKind::Builtin ||
        payload->implementation.entrypoint != effects::builtin_effect::CameraTransformEntrypoint) {
      continue;
    }

    effectNodeId = effectNode->id;
    return payload;
  }
  return nullptr;
}

foundation::Result<std::optional<foundation::KeyframeId>> NativeStewardPlanner::effectParamKeyframeIdAtTime(
  const timeline::EffectPayload& payload,
  const std::string& paramName,
  foundation::TimeSeconds time
) const {
  const auto param = std::find_if(payload.params.values.begin(), payload.params.values.end(), [&](const timeline::Param& value) {
    return value.name == paramName;
  });
  if (param == payload.params.values.end()) {
    return foundation::Error{
      "steward.camera_transform_param_missing",
      "Camera Transform controls are missing the requested parameter."
    };
  }

  for (const timeline::Param::Keyframe& keyframe : param->keyframes) {
    if (keyframe.time == time) {
      return std::optional<foundation::KeyframeId>{keyframe.id};
    }
  }
  return std::optional<foundation::KeyframeId>{};
}

foundation::Result<std::vector<CameraTransformParamAdjustment>> NativeStewardPlanner::cameraTransformParamAdjustmentsForIntent(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  const std::string& intent
) const {
  const graph::GraphNode* cameraNode = snapshot.graph.findNode(cameraNodeId);
  if (cameraNode == nullptr || cameraNode->kind != graph::NodeKind::Camera) {
    return foundation::Error{
      "steward.camera_missing",
      "Steward camera control adjustment requires an existing camera node."
    };
  }

  foundation::NodeId effectNodeId;
  const timeline::EffectPayload* payload = cameraTransformEffectPayload(snapshot, cameraNodeId, effectNodeId);
  if (payload == nullptr) {
    return foundation::Error{
      "steward.camera_transform_missing",
      "Steward camera control adjustment requires existing Camera Transform controls."
    };
  }

  const std::string normalized = lowercaseAscii(intent);
  const double strength = intentStrengthMultiplier(normalized);
  std::vector<CameraTransformParamAdjustment> adjustments;

  auto addAdjustment = [&](std::vector<CameraTransformParamAdjustment>& currentAdjustments,
                           std::string adjustmentParamName,
                           CameraTransformAdjustmentOperation adjustmentOperation,
                           double adjustmentOperand) -> foundation::Result<void> {
    auto adjustment = cameraTransformParamAdjustment(
      *payload,
      effectNodeId,
      std::move(adjustmentParamName),
      adjustmentOperation,
      adjustmentOperand
    );
    if (!adjustment) {
      return adjustment.error();
    }
    if (adjustment.value().has_value()) {
      currentAdjustments.push_back(std::move(adjustment.value().value()));
    }
    return {};
  };

  const bool centerRequested = cameraIntentRequestsCenter(normalized);
  const bool resetRequested = cameraIntentRequestsReset(normalized);
  if (centerRequested || resetRequested) {
    auto positionX = addAdjustment(
      adjustments,
      effects::builtin_effect::PositionXParam,
      CameraTransformAdjustmentOperation::Set,
      CenteredCameraTransformPositionX
    );
    if (!positionX) {
      return positionX.error();
    }
    auto positionY = addAdjustment(
      adjustments,
      effects::builtin_effect::PositionYParam,
      CameraTransformAdjustmentOperation::Set,
      CenteredCameraTransformPositionY
    );
    if (!positionY) {
      return positionY.error();
    }
    if (resetRequested) {
      auto zoom = addAdjustment(
        adjustments,
        effects::builtin_effect::ZoomParam,
        CameraTransformAdjustmentOperation::Set,
        NormalCameraTransformZoom
      );
      if (!zoom) {
        return zoom.error();
      }
      return adjustments;
    }
  }

  if (!centerRequested) {
    if (containsAsciiWord(normalized, "left")) {
      auto positionX = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionXParam,
        CameraTransformAdjustmentOperation::Add,
        -CameraTransformPositionXStep * strength
      );
      if (!positionX) {
        return positionX.error();
      }
    } else if (containsAsciiWord(normalized, "right")) {
      auto positionX = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionXParam,
        CameraTransformAdjustmentOperation::Add,
        CameraTransformPositionXStep * strength
      );
      if (!positionX) {
        return positionX.error();
      }
    }

    if (containsAsciiWord(normalized, "up")) {
      auto positionY = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionYParam,
        CameraTransformAdjustmentOperation::Add,
        -CameraTransformPositionYStep * strength
      );
      if (!positionY) {
        return positionY.error();
      }
    } else if (containsAsciiWord(normalized, "down")) {
      auto positionY = addAdjustment(
        adjustments,
        effects::builtin_effect::PositionYParam,
        CameraTransformAdjustmentOperation::Add,
        CameraTransformPositionYStep * strength
      );
      if (!positionY) {
        return positionY.error();
      }
    }
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    auto zoom = addAdjustment(
      adjustments,
      effects::builtin_effect::ZoomParam,
      CameraTransformAdjustmentOperation::Multiply,
      1.0 - CameraTransformZoomOutStep * strength
    );
    if (!zoom) {
      return zoom.error();
    }
  } else if (cameraIntentRequestsZoomIn(normalized)) {
    auto zoom = addAdjustment(
      adjustments,
      effects::builtin_effect::ZoomParam,
      CameraTransformAdjustmentOperation::Multiply,
      1.0 + CameraTransformZoomInStep * strength
    );
    if (!zoom) {
      return zoom.error();
    }
  }

  if (adjustments.empty()) {
    return foundation::Error{
      "steward.camera_transform_intent_unknown",
      "Camera Transform adjustments must explicitly mention center, reset, left, right, up, down, zoom, bigger, or smaller."
    };
  }
  return adjustments;
}

foundation::Result<std::vector<CameraTransformKeyframeAdjustment>> NativeStewardPlanner::adjustedCameraTransformKeyframes(
  const project::ProjectSnapshot& snapshot,
  const CameraTransformParamAdjustment& adjustment
) const {
  const graph::GraphNode* effectNode = snapshot.graph.findNode(adjustment.effectNodeId);
  if (effectNode == nullptr || effectNode->kind != graph::NodeKind::Effect) {
    return foundation::Error{
      "steward.camera_transform_effect_missing",
      "Camera Transform keyframe adjustment requires an existing effect node."
    };
  }
  const auto* payload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
  if (payload == nullptr) {
    return foundation::Error{
      "steward.camera_transform_effect_payload_missing",
      "Camera Transform keyframe adjustment requires an effect payload."
    };
  }

  const auto param = std::find_if(payload->params.values.begin(), payload->params.values.end(), [&](const timeline::Param& value) {
    return value.name == adjustment.paramName;
  });
  if (param == payload->params.values.end()) {
    return foundation::Error{
      "steward.camera_transform_param_missing",
      "Camera Transform controls are missing the requested parameter."
    };
  }

  std::vector<CameraTransformKeyframeAdjustment> keyframes;
  keyframes.reserve(param->keyframes.size());
  for (const timeline::Param::Keyframe& keyframe : param->keyframes) {
    const auto* numericValue = std::get_if<double>(&keyframe.value);
    if (numericValue == nullptr) {
      return foundation::Error{
        "steward.camera_transform_keyframe_not_numeric",
        "Camera Transform keyframe adjustment requires numeric keyframes."
      };
    }
    keyframes.push_back(CameraTransformKeyframeAdjustment{
      keyframe.id,
      keyframe.time,
      applyCameraTransformOperation(*numericValue, adjustment.operation, adjustment.operand)
    });
  }
  return keyframes;
}

foundation::Result<CameraTransformMotionKeyframes> NativeStewardPlanner::cameraTransformMotionAdjustmentForIntent(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  const std::string& intent,
  foundation::TimeRange activeRange
) const {
  if (activeRange.end.value <= activeRange.start.value) {
    return foundation::Error{
      "steward.camera_transform_motion_range_invalid",
      "Camera Transform motion adjustment requires a positive active range."
    };
  }

  auto adjustments = cameraTransformParamAdjustmentsForIntent(snapshot, cameraNodeId, intent);
  if (!adjustments) {
    return adjustments.error();
  }
  if (adjustments.value().empty()) {
    return foundation::Error{
      "steward.camera_transform_noop",
      "Camera Transform controls already match the requested adjustment."
    };
  }
  if (adjustments.value().size() != 1) {
    return foundation::Error{
      "steward.camera_transform_motion_multi_param",
      "Camera Transform motion adjustments must target one parameter."
    };
  }
  const CameraTransformParamAdjustment& adjustment = adjustments.value().front();

  foundation::NodeId effectNodeId;
  const timeline::EffectPayload* payload = cameraTransformEffectPayload(snapshot, cameraNodeId, effectNodeId);
  if (payload == nullptr) {
    return foundation::Error{
      "steward.camera_transform_missing",
      "Steward camera control adjustment requires existing Camera Transform controls."
    };
  }

  auto currentValue = numericEffectParamValue(*payload, adjustment.paramName);
  if (!currentValue) {
    return currentValue.error();
  }

  return CameraTransformMotionKeyframes{
    adjustment.paramName,
    currentValue.value(),
    adjustment.value,
    activeRange.end
  };
}

} // namespace grapple::app
