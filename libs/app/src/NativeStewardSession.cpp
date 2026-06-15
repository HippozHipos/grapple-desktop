#include <grapple/app/NativeStewardSession.hpp>

#include <grapple/agent/AgentBridge.hpp>
#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/effects/BuiltinEffects.hpp>
#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Json.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace grapple::app {

namespace {

constexpr const char CanonicalEffectCreateNodeToolId[] = "effect.create_node";
constexpr const char CanonicalEffectCreateParamKeyframeToolId[] = "effect.create_param_keyframe";
constexpr const char CanonicalEffectUpdateParamKeyframeToolId[] = "effect.update_param_keyframe";
constexpr const char CanonicalPlaceAssetToolId[] = "timeline.place_asset";
constexpr const char CanonicalUpdateClipTransformToolId[] = "timeline.update_clip_transform";
constexpr const char CanonicalUpdateEffectParamToolId[] = "effect.update_param_value";
constexpr double CenteredCameraTransformPositionX = 0.0;
constexpr double CenteredCameraTransformPositionY = 0.0;
constexpr double NormalCameraTransformZoom = 1.0;

struct CameraTransformIntentDefaults {
  double positionX = CenteredCameraTransformPositionX;
  double positionY = CenteredCameraTransformPositionY;
  double zoom = NormalCameraTransformZoom;
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

CameraTransformIntentDefaults cameraTransformDefaultsForIntent(const std::string& intent) {
  const std::string normalized = lowercaseAscii(intent);
  CameraTransformIntentDefaults defaults;

  if (containsAsciiWord(normalized, "left")) {
    defaults.positionX = -0.25;
  } else if (containsAsciiWord(normalized, "right")) {
    defaults.positionX = 0.25;
  }

  if (containsAsciiWord(normalized, "up")) {
    defaults.positionY = -0.2;
  } else if (containsAsciiWord(normalized, "down")) {
    defaults.positionY = 0.2;
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    defaults.zoom = 0.8;
  } else if (cameraIntentRequestsZoomIn(normalized)) {
    defaults.zoom = 1.5;
  } else if (containsText(normalized, "subject")) {
    defaults.zoom = 1.1;
  }

  return defaults;
}

std::optional<CameraTransformMotionKeyframes> cameraMotionKeyframesForIntent(
  const std::string& intent,
  foundation::TimeRange activeRange
) {
  if (activeRange.end.value <= activeRange.start.value) {
    return std::nullopt;
  }

  const std::string normalized = lowercaseAscii(intent);
  if (!cameraIntentRequestsTemporalMotion(normalized)) {
    return std::nullopt;
  }

  if (containsAsciiWord(normalized, "left")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionXParam,
      CenteredCameraTransformPositionX,
      -0.25,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "right")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionXParam,
      CenteredCameraTransformPositionX,
      0.25,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "up")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionYParam,
      CenteredCameraTransformPositionY,
      -0.2,
      activeRange.end
    };
  }
  if (containsAsciiWord(normalized, "down")) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::PositionYParam,
      CenteredCameraTransformPositionY,
      0.2,
      activeRange.end
    };
  }

  if (cameraIntentRequestsZoomOut(normalized)) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::ZoomParam,
      NormalCameraTransformZoom,
      0.8,
      activeRange.end
    };
  }
  if (containsText(normalized, "zoom") || cameraIntentRequestsZoomIn(normalized)) {
    return CameraTransformMotionKeyframes{
      effects::builtin_effect::ZoomParam,
      NormalCameraTransformZoom,
      1.5,
      activeRange.end
    };
  }

  return std::nullopt;
}

bool cameraIntentRequestsExplicitMotion(const std::string& intent) {
  return cameraIntentRequestsTemporalMotion(lowercaseAscii(intent));
}

foundation::Result<timeline::Transform2D> clipTransformForIntent(
  const timeline::Transform2D& current,
  const std::string& intent
) {
  const std::string normalized = lowercaseAscii(intent);
  timeline::Transform2D transform = current;
  bool changed = false;

  if (containsAsciiWord(normalized, "left")) {
    transform.position.x -= 0.25;
    changed = true;
  } else if (containsAsciiWord(normalized, "right")) {
    transform.position.x += 0.25;
    changed = true;
  }

  if (containsAsciiWord(normalized, "up")) {
    transform.position.y -= 0.2;
    changed = true;
  } else if (containsAsciiWord(normalized, "down")) {
    transform.position.y += 0.2;
    changed = true;
  }

  if (containsText(normalized, "scale down") ||
      containsAsciiWord(normalized, "smaller") ||
      containsAsciiWord(normalized, "shrink")) {
    transform.scale.x *= 0.75;
    transform.scale.y *= 0.75;
    changed = true;
  } else if (containsText(normalized, "scale up") ||
             containsAsciiWord(normalized, "larger") ||
             containsAsciiWord(normalized, "bigger")) {
    transform.scale.x *= 1.25;
    transform.scale.y *= 1.25;
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

const timeline::EffectPayload* cameraTransformEffectPayload(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  foundation::NodeId& effectNodeId
) {
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

foundation::Result<std::optional<foundation::KeyframeId>> effectParamKeyframeIdAtTime(
  const timeline::EffectPayload& payload,
  const std::string& paramName,
  foundation::TimeSeconds time
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

  for (const timeline::Param::Keyframe& keyframe : param->keyframes) {
    if (keyframe.time == time) {
      return std::optional<foundation::KeyframeId>{keyframe.id};
    }
  }
  return std::optional<foundation::KeyframeId>{};
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

foundation::Result<std::vector<CameraTransformParamAdjustment>> cameraTransformParamAdjustmentsForIntent(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  const std::string& intent
) {
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
  std::string paramName;
  double operand = 0.0;
  CameraTransformAdjustmentOperation operation = CameraTransformAdjustmentOperation::Add;

  auto addAdjustment = [&](std::vector<CameraTransformParamAdjustment>& adjustments,
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
      adjustments.push_back(std::move(adjustment.value().value()));
    }
    return {};
  };

  const bool centerRequested = cameraIntentRequestsCenter(normalized);
  const bool resetRequested = cameraIntentRequestsReset(normalized);
  if (centerRequested || resetRequested) {
    std::vector<CameraTransformParamAdjustment> adjustments;
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
    }
    return adjustments;
  }

  if (containsAsciiWord(normalized, "left")) {
    paramName = effects::builtin_effect::PositionXParam;
    operand = -0.25;
  } else if (containsAsciiWord(normalized, "right")) {
    paramName = effects::builtin_effect::PositionXParam;
    operand = 0.25;
  } else if (containsAsciiWord(normalized, "up")) {
    paramName = effects::builtin_effect::PositionYParam;
    operand = -0.2;
  } else if (containsAsciiWord(normalized, "down")) {
    paramName = effects::builtin_effect::PositionYParam;
    operand = 0.2;
  } else if (cameraIntentRequestsZoomOut(normalized)) {
    paramName = effects::builtin_effect::ZoomParam;
    operand = 0.8;
    operation = CameraTransformAdjustmentOperation::Multiply;
  } else if (cameraIntentRequestsZoomIn(normalized)) {
    paramName = effects::builtin_effect::ZoomParam;
    operand = 1.25;
    operation = CameraTransformAdjustmentOperation::Multiply;
  } else {
    return foundation::Error{
      "steward.camera_transform_intent_unknown",
      "Camera Transform adjustments must explicitly mention center, reset, left, right, up, down, zoom, bigger, or smaller."
    };
  }

  std::vector<CameraTransformParamAdjustment> adjustments;
  auto adjustment = addAdjustment(adjustments, std::move(paramName), operation, operand);
  if (!adjustment) {
    return adjustment.error();
  }
  return adjustments;
}

foundation::Result<std::vector<CameraTransformKeyframeAdjustment>> adjustedCameraTransformKeyframes(
  const project::ProjectSnapshot& snapshot,
  const CameraTransformParamAdjustment& adjustment
) {
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

foundation::Result<CameraTransformMotionKeyframes> cameraTransformMotionAdjustmentForIntent(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId,
  const std::string& intent,
  foundation::TimeRange activeRange
) {
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

std::string runStartedPayload(const std::string& title) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "title", title);
  payload << '}';
  return payload.str();
}

std::string modelMessagePayload(std::string role, std::string content) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "role", role);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "content", content);
  payload << '}';
  return payload.str();
}

std::string cameraTransformEffectCreateArgumentsPayload(
  const foundation::NodeId& cameraNodeId,
  foundation::TimeRange activeRange,
  CameraTransformIntentDefaults defaults
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "targetNodeId", cameraNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "displayName", effects::builtin_effect::CameraTransformDisplayName);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "implementationKind", "builtin");
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "language", "builtin");
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "entrypoint", effects::builtin_effect::CameraTransformEntrypoint);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "source", effects::builtin_effect::CameraTransformSource);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "sourcePort", effects::output_name::CameraTransform);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "targetPort", "input");
  arguments << ",\"inputPorts\":[\"frame\"]";
  arguments << ",\"outputPorts\":[";
  arguments << foundation::jsonQuoted(effects::output_name::CameraTransform);
  arguments << "]";
  arguments << ",\"activeRange\":{";
  arguments << "\"start\":" << activeRange.start.value;
  arguments << ",\"end\":" << activeRange.end.value;
  arguments << "}";
  arguments << ",\"params\":[";
  arguments << "{\"name\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionXParam);
  arguments << ",\"label\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionXLabel);
  arguments << ",\"value\":" << defaults.positionX;
  arguments << ",\"numeric\":{\"min\":-1,\"max\":1,\"step\":0.01}}";
  arguments << ",{\"name\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionYParam);
  arguments << ",\"label\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionYLabel);
  arguments << ",\"value\":" << defaults.positionY;
  arguments << ",\"numeric\":{\"min\":-1,\"max\":1,\"step\":0.01}}";
  arguments << ",{\"name\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::ZoomParam);
  arguments << ",\"label\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::ZoomLabel);
  arguments << ",\"value\":" << defaults.zoom;
  arguments << ",\"numeric\":{\"min\":0.25,\"max\":4,\"step\":0.01}}";
  arguments << "]";
  arguments << '}';
  return arguments.str();
}

std::string effectCreateParamKeyframeArgumentsPayload(
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  foundation::TimeSeconds time,
  double value
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "effectNodeId", effectNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "paramName", paramName);
  arguments << ",\"time\":" << time.value;
  arguments << ",\"value\":" << value;
  arguments << '}';
  return arguments.str();
}

std::string effectUpdateParamKeyframeArgumentsPayload(
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  const foundation::KeyframeId& keyframeId,
  foundation::TimeSeconds time,
  double value
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "effectNodeId", effectNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "paramName", paramName);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "keyframeId", keyframeId.value());
  arguments << ",\"time\":" << time.value;
  arguments << ",\"value\":" << value;
  arguments << '}';
  return arguments.str();
}

std::string diagnosticPayload(const foundation::Error& error) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "code", error.code);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "severity", "error");
  payload << ',';
  foundation::writeJsonStringProperty(payload, "message", error.message);
  payload << '}';
  return payload.str();
}

std::string runFinishedPayload(const std::string& status, const std::string& summary) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "status", status);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "summary", summary);
  payload << '}';
  return payload.str();
}

std::int64_t stewardRunNumber(const foundation::RunId& runId) {
  constexpr std::string_view prefix{"run_steward_"};
  if (runId.value().rfind(prefix, 0) != 0) {
    return 0;
  }
  const std::string suffix = runId.value().substr(prefix.size());
  if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(), [](unsigned char character) {
    return std::isdigit(character) != 0;
  })) {
    return 0;
  }
  return std::stoll(suffix);
}

foundation::ToolId stewardToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_camera_transform_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardKeyframeToolCallIdForRun(const foundation::RunId& runId, std::int64_t index) {
  return foundation::ToolId{
    "tool_steward_camera_transform_keyframe_" + std::to_string(stewardRunNumber(runId)) + "_" + std::to_string(index)
  };
}

foundation::ToolId stewardPlaceAssetToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_place_asset_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardClipTransformToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_clip_transform_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardCameraTransformParamToolCallIdForRun(const foundation::RunId& runId, std::int64_t index) {
  return foundation::ToolId{
    "tool_steward_camera_transform_param_" +
    std::to_string(stewardRunNumber(runId)) +
    "_" +
    std::to_string(index)
  };
}

std::string placeAssetArgumentsPayload(
  const foundation::AssetId& assetId,
  const std::optional<foundation::TimeSeconds>& duration
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "assetId", assetId.value());
  if (duration.has_value()) {
    arguments << ",\"duration\":" << duration->value;
  }
  arguments << '}';
  return arguments.str();
}

std::string clipTransformArgumentsPayload(
  const foundation::NodeId& clipNodeId,
  const timeline::Transform2D& transform
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "clipNodeId", clipNodeId.value());
  arguments << ",\"position\":{";
  arguments << "\"x\":" << transform.position.x;
  arguments << ",\"y\":" << transform.position.y;
  arguments << "}";
  arguments << ",\"scale\":{";
  arguments << "\"x\":" << transform.scale.x;
  arguments << ",\"y\":" << transform.scale.y;
  arguments << "}";
  arguments << ",\"rotationDegrees\":" << transform.rotationDegrees;
  arguments << ",\"opacity\":" << transform.opacity;
  arguments << '}';
  return arguments.str();
}

std::string effectParamValueArgumentsPayload(
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  double value
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "effectNodeId", effectNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "paramName", paramName);
  arguments << ",\"value\":" << value;
  arguments << '}';
  return arguments.str();
}

foundation::Result<agent::ToolResult> dispatchEffectKeyframeToolCall(
  agent::AgentBridge& bridge,
  const foundation::RunId& runId,
  const foundation::ProjectId& projectId,
  const foundation::RevisionId& expectedRevision,
  const foundation::ToolId& toolCallId,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  const CameraTransformKeyframeAdjustment& keyframe
) {
  if (keyframe.keyframeId.has_value()) {
    return bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
      runId,
      projectId,
      expectedRevision,
      toolCallId,
      CanonicalEffectUpdateParamKeyframeToolId,
      effectUpdateParamKeyframeArgumentsPayload(
        effectNodeId,
        paramName,
        keyframe.keyframeId.value(),
        keyframe.time,
        keyframe.value
      )
    });
  }

  return bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId,
    projectId,
    expectedRevision,
    toolCallId,
    CanonicalEffectCreateParamKeyframeToolId,
    effectCreateParamKeyframeArgumentsPayload(
      effectNodeId,
      paramName,
      keyframe.time,
      keyframe.value
    )
  });
}

class CommittingAgentCommandService final : public project::IProjectCommandService {
public:
  CommittingAgentCommandService(
    NativeProjectSession& project,
    NativeProjectCommandWriter& commandWriter,
    std::string snapshotLabel,
    std::optional<storage::ProjectPackageSessionResult>& packageResult,
    std::optional<foundation::NodeId>* placedClipNodeId = nullptr
  )
    : project_{project},
      commandWriter_{commandWriter},
      snapshotLabel_{std::move(snapshotLabel)},
      packageResult_{packageResult},
      placedClipNodeId_{placedClipNodeId} {}

  foundation::Result<project::ProjectCommandResult> apply(
    const project::ProjectCommandEnvelope& command
  ) override {
    if (placedClipNodeId_ != nullptr) {
      if (const auto* placement = std::get_if<project::AddMediaToTimelineCommand>(&command.payload)) {
        *placedClipNodeId_ = placement->clip.nodeId;
      }
    }

    const foundation::SnapshotId snapshotId = commandWriter_.nextSnapshotId("steward edit");
    project::ProjectCommandEnvelope stewardCommand = command;
    stewardCommand.source = project::CommandSource{
      project::CommandSourceKind::Agent,
      command.source.runId,
      "steward"
    };

    auto committed = project_.applyAndCommit(
      stewardCommand,
      storage::ProjectCommitRecordOptions{
        std::chrono::system_clock::now(),
        storage::SnapshotCommitRecord{
          snapshotId,
          foundation::FilePath{"snapshots/" + snapshotId.value() + ".json"},
          snapshotLabel_
        }
      }
    );
    if (!committed) {
      return committed.error();
    }

    packageResult_ = committed.value();
    return committed.value().commandResult;
  }

private:
  NativeProjectSession& project_;
  NativeProjectCommandWriter& commandWriter_;
  std::string snapshotLabel_;
  std::optional<storage::ProjectPackageSessionResult>& packageResult_;
  std::optional<foundation::NodeId>* placedClipNodeId_;
};

} // namespace

NativeStewardSession::NativeStewardSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter)
  : project_{project},
    commandWriter_{commandWriter} {}

foundation::Result<void> NativeStewardSession::finishRunWithError(
  const foundation::RunId& runId,
  const foundation::Error& error
) {
  markRunStatus(runId, agent::AgentRunStatus::Failed);
  auto diagnostic = appendEvent(runId, agent::AgentRunEventKind::DiagnosticEmitted, diagnosticPayload(error));
  if (!diagnostic) {
    return diagnostic.error();
  }
  auto runFinished = appendEvent(
    runId,
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("failed", error.message)
  );
  if (!runFinished) {
    return runFinished.error();
  }
  return {};
}

foundation::Result<NativeStewardMediaPlacementResult> NativeStewardSession::placeAssetOnTimeline(
  foundation::AssetId assetId,
  std::optional<foundation::TimeSeconds> duration
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  std::string title = "Add selected media to the timeline.";
  const asset::Asset* selectedAsset = snapshot.value().assets.find(assetId);
  if (selectedAsset != nullptr) {
    title = "Add " + selectedAsset->name + " to the timeline.";
  }

  auto runId = startRun(snapshot.value(), title);
  if (!runId) {
    return runId.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Placing the selected media on the timeline as an editable graph change.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  std::optional<foundation::NodeId> placedClipNodeId;
  CommittingAgentCommandService stewardCommands{
    project_,
    commandWriter_,
    title,
    packageResult,
    &placedClipNodeId
  };
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardPlaceAssetToolCallIdForRun(runId.value()),
    CanonicalPlaceAssetToolId,
    placeAssetArgumentsPayload(assetId, duration)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value() || !placedClipNodeId.has_value()) {
    const foundation::Error error{
      "steward.media_placement_result_missing",
      "Steward media placement tool succeeded without a committed placement result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Added selected media to the timeline.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return NativeStewardMediaPlacementResult{packageResult.value(), placedClipNodeId.value()};
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::transformClip(
  foundation::NodeId clipNodeId,
  std::string intent
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const graph::GraphNode* clipNode = snapshot.value().graph.findNode(clipNodeId);
  if (clipNode == nullptr || clipNode->kind != graph::NodeKind::Clip) {
    const foundation::Error error{"steward.clip_missing", "Steward clip transform requires an existing clip node."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  const auto* clipPayload = std::get_if<timeline::ClipPayload>(&clipNode->payload);
  if (clipPayload == nullptr) {
    const foundation::Error error{"steward.clip_payload_missing", "Steward clip transform requires a clip payload."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  auto nextTransform = clipTransformForIntent(clipPayload->transform, intent);
  if (!nextTransform) {
    auto finished = finishRunWithError(runId.value(), nextTransform.error());
    if (!finished) {
      return finished.error();
    }
    return nextTransform.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Updating the selected clip transform as an editable graph change.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardClipTransformToolCallIdForRun(runId.value()),
    CanonicalUpdateClipTransformToolId,
    clipTransformArgumentsPayload(clipNodeId, nextTransform.value())
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.clip_transform_result_missing",
      "Steward clip transform tool succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Updated selected clip transform.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::adjustCameraTransformControls(
  foundation::NodeId cameraNodeId,
  std::string intent,
  foundation::TimeRange activeRange
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const bool motionRequested = cameraIntentRequestsExplicitMotion(intent);
  std::optional<CameraTransformMotionKeyframes> motion;
  std::vector<CameraTransformParamAdjustment> adjustments;
  std::vector<std::vector<CameraTransformKeyframeAdjustment>> keyframeAdjustmentsByParam;
  if (motionRequested) {
    auto motionAdjustment = cameraTransformMotionAdjustmentForIntent(snapshot.value(), cameraNodeId, intent, activeRange);
    if (!motionAdjustment) {
      auto finished = finishRunWithError(runId.value(), motionAdjustment.error());
      if (!finished) {
        return finished.error();
      }
      return motionAdjustment.error();
    }
    motion = motionAdjustment.value();
  } else {
    auto paramAdjustments = cameraTransformParamAdjustmentsForIntent(snapshot.value(), cameraNodeId, intent);
    if (!paramAdjustments) {
      auto finished = finishRunWithError(runId.value(), paramAdjustments.error());
      if (!finished) {
        return finished.error();
      }
      return paramAdjustments.error();
    }
    if (paramAdjustments.value().empty()) {
      const foundation::Error error{
        "steward.camera_transform_noop",
        "Camera Transform controls already match the requested adjustment."
      };
      auto finished = finishRunWithError(runId.value(), error);
      if (!finished) {
        return finished.error();
      }
      return error;
    }
    adjustments = std::move(paramAdjustments.value());
    keyframeAdjustmentsByParam.reserve(adjustments.size());
    for (const CameraTransformParamAdjustment& paramAdjustment : adjustments) {
      auto adjustedKeyframes = adjustedCameraTransformKeyframes(snapshot.value(), paramAdjustment);
      if (!adjustedKeyframes) {
        auto finished = finishRunWithError(runId.value(), adjustedKeyframes.error());
        if (!finished) {
          return finished.error();
        }
        return adjustedKeyframes.error();
      }
      keyframeAdjustmentsByParam.push_back(std::move(adjustedKeyframes.value()));
    }
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload(
      "assistant",
      motion.has_value()
        ? "Updating the existing Camera Transform controls as editable motion keyframes."
        : std::any_of(keyframeAdjustmentsByParam.begin(), keyframeAdjustmentsByParam.end(), [](const auto& keyframes) { return !keyframes.empty(); })
          ? "Updating the existing Camera Transform keyframes as an editable parameter change."
          : "Updating the existing Camera Transform controls as an editable parameter change."
    )
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  foundation::RevisionId latestRevision = snapshot.value().revision;
  if (motion.has_value()) {
    foundation::NodeId effectNodeId;
    const timeline::EffectPayload* payload = cameraTransformEffectPayload(snapshot.value(), cameraNodeId, effectNodeId);
    if (payload == nullptr) {
      const foundation::Error error{
        "steward.camera_transform_missing",
        "Steward camera control adjustment requires existing Camera Transform controls."
      };
      auto finished = finishRunWithError(runId.value(), error);
      if (!finished) {
        return finished.error();
      }
      return error;
    }
    auto startKeyframeId = effectParamKeyframeIdAtTime(*payload, motion->paramName, activeRange.start);
    if (!startKeyframeId) {
      auto finished = finishRunWithError(runId.value(), startKeyframeId.error());
      if (!finished) {
        return finished.error();
      }
      return startKeyframeId.error();
    }
    auto endKeyframeId = effectParamKeyframeIdAtTime(*payload, motion->paramName, motion->endTime);
    if (!endKeyframeId) {
      auto finished = finishRunWithError(runId.value(), endKeyframeId.error());
      if (!finished) {
        return finished.error();
      }
      return endKeyframeId.error();
    }

    auto startKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 1),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        startKeyframeId.value(),
        activeRange.start,
        motion->startValue
      }
    );
    if (!startKeyframe) {
      auto finished = finishRunWithError(runId.value(), startKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return startKeyframe.error();
    }
    latestRevision = startKeyframe.value().observedRevision;

    auto endKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 2),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        endKeyframeId.value(),
        motion->endTime,
        motion->endValue
      }
    );
    if (!endKeyframe) {
      auto finished = finishRunWithError(runId.value(), endKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return endKeyframe.error();
    }
  } else {
    std::int64_t toolCallIndex = 1;
    for (std::size_t adjustmentIndex = 0; adjustmentIndex < adjustments.size(); ++adjustmentIndex) {
      const CameraTransformParamAdjustment& paramAdjustment = adjustments[adjustmentIndex];
      const std::vector<CameraTransformKeyframeAdjustment>& keyframeAdjustments = keyframeAdjustmentsByParam[adjustmentIndex];
      if (!keyframeAdjustments.empty()) {
        for (const CameraTransformKeyframeAdjustment& keyframe : keyframeAdjustments) {
          auto keyframeResult = dispatchEffectKeyframeToolCall(
            bridge,
            runId.value(),
            snapshot.value().info.id,
            latestRevision,
            stewardKeyframeToolCallIdForRun(runId.value(), toolCallIndex),
            paramAdjustment.effectNodeId,
            paramAdjustment.paramName,
            keyframe
          );
          if (!keyframeResult) {
            auto finished = finishRunWithError(runId.value(), keyframeResult.error());
            if (!finished) {
              return finished.error();
            }
            return keyframeResult.error();
          }
          latestRevision = keyframeResult.value().observedRevision;
          ++toolCallIndex;
        }
      } else {
        auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
          runId.value(),
          snapshot.value().info.id,
          latestRevision,
          stewardCameraTransformParamToolCallIdForRun(runId.value(), toolCallIndex),
          CanonicalUpdateEffectParamToolId,
          effectParamValueArgumentsPayload(
            paramAdjustment.effectNodeId,
            paramAdjustment.paramName,
            paramAdjustment.value
          )
        });
        if (!dispatched) {
          auto finished = finishRunWithError(runId.value(), dispatched.error());
          if (!finished) {
            return finished.error();
          }
          return dispatched.error();
        }
        latestRevision = dispatched.value().observedRevision;
        ++toolCallIndex;
      }
    }
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.camera_transform_update_result_missing",
      "Steward camera control update succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload(
      "succeeded",
      motion.has_value()
        ? "Updated existing Camera Transform motion keyframes."
        : std::any_of(keyframeAdjustmentsByParam.begin(), keyframeAdjustmentsByParam.end(), [](const auto& keyframes) { return !keyframes.empty(); })
          ? "Updated existing Camera Transform keyframes."
          : "Updated existing Camera Transform controls."
    )
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::createCameraTransformEffect(
  foundation::NodeId cameraNodeId,
  std::string intent,
  foundation::TimeRange activeRange
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  foundation::NodeId existingEffectNodeId;
  if (cameraTransformEffectPayload(snapshot.value(), cameraNodeId, existingEffectNodeId) != nullptr) {
    const foundation::Error error{
      "agent.camera_transform_exists",
      "Camera already has Camera Transform controls."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Creating an editable Camera Transform effect for the selected camera.")
  );
  if (!message) {
    return message.error();
  }

  const foundation::ToolId toolCallId = stewardToolCallIdForRun(runId.value());
  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  const CameraTransformIntentDefaults defaults = cameraTransformDefaultsForIntent(intent);
  const std::optional<CameraTransformMotionKeyframes> motion = cameraMotionKeyframesForIntent(intent, activeRange);
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    toolCallId,
    CanonicalEffectCreateNodeToolId,
    cameraTransformEffectCreateArgumentsPayload(cameraNodeId, activeRange, defaults)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  foundation::RevisionId latestRevision = dispatched.value().observedRevision;

  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.package_result_missing",
      "Steward camera transform effect creation succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  foundation::NodeId effectNodeId;
  const timeline::EffectPayload* createdPayload = cameraTransformEffectPayload(
    packageResult->snapshot,
    cameraNodeId,
    effectNodeId
  );
  if (createdPayload == nullptr) {
    const foundation::Error error{
      "steward.camera_transform_effect_missing",
      "Created Camera Transform effect was not found on the target camera."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  if (motion.has_value()) {
    auto startKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 1),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        std::nullopt,
        activeRange.start,
        motion->startValue
      }
    );
    if (!startKeyframe) {
      auto finished = finishRunWithError(runId.value(), startKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return startKeyframe.error();
    }
    latestRevision = startKeyframe.value().observedRevision;

    auto endKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 2),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        std::nullopt,
        motion->endTime,
        motion->endValue
      }
    );
    if (!endKeyframe) {
      auto finished = finishRunWithError(runId.value(), endKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return endKeyframe.error();
    }
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload(
      "succeeded",
      motion.has_value()
        ? "Created editable Camera Transform parameters and motion keyframes."
        : "Created editable Camera Transform parameters."
    )
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

agent::AgentConversationState NativeStewardSession::conversationState() const {
  const agent::AgentConversationStateProjector projector;
  return projector.project(runs_, events_.records());
}

const std::vector<agent::AgentRun>& NativeStewardSession::runs() const noexcept {
  return runs_;
}

const std::vector<agent::AgentRunEvent>& NativeStewardSession::events() const noexcept {
  return events_.records();
}

foundation::Result<void> NativeStewardSession::restoreConversation(
  std::vector<agent::AgentRun> runs,
  std::vector<agent::AgentRunEvent> events
) {
  agent::AgentRunEventLog restoredEvents;
  for (const agent::AgentRunEvent& event : events) {
    auto appended = restoredEvents.append(event);
    if (!appended) {
      return appended.error();
    }
  }

  std::int64_t nextRunNumber = 1;
  for (const agent::AgentRun& run : runs) {
    nextRunNumber = std::max(nextRunNumber, stewardRunNumber(run.id) + 1);
  }

  std::int64_t nextSequence = 1;
  for (const agent::AgentRunEvent& event : restoredEvents.records()) {
    nextSequence = std::max(nextSequence, event.sequence + 1);
  }

  runs_ = std::move(runs);
  events_ = std::move(restoredEvents);
  nextRunNumber_ = nextRunNumber;
  nextSequence_ = nextSequence;
  return {};
}

foundation::Result<foundation::RunId> NativeStewardSession::startRun(
  const project::ProjectSnapshot& snapshot,
  const std::string& title
) {
  const foundation::RunId runId{"run_steward_" + std::to_string(nextRunNumber_++)};
  auto started = appendEvent(runId, agent::AgentRunEventKind::RunStarted, runStartedPayload(title));
  if (!started) {
    return started.error();
  }
  runs_.push_back(agent::AgentRun{
    runId,
    snapshot.info.id,
    std::nullopt,
    agent::AgentRunStatus::Running,
    std::chrono::system_clock::now()
  });
  return runId;
}

foundation::Result<void> NativeStewardSession::appendEvent(
  foundation::RunId runId,
  agent::AgentRunEventKind kind,
  std::string payloadJson
) {
  auto appended = events_.append(agent::AgentRunEvent{
    std::move(runId),
    nextSequence_,
    kind,
    std::move(payloadJson),
    std::chrono::system_clock::now()
  });
  if (!appended) {
    return appended.error();
  }
  ++nextSequence_;
  return {};
}

void NativeStewardSession::markRunStatus(const foundation::RunId& runId, agent::AgentRunStatus status) {
  for (agent::AgentRun& run : runs_) {
    if (run.id == runId) {
      run.status = status;
      return;
    }
  }
}

} // namespace grapple::app
