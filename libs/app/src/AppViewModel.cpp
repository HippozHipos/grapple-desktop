#include <grapple/app/AppViewModel.hpp>
#include <grapple/timeline/ParamSampling.hpp>

#include <sstream>
#include <type_traits>
#include <variant>

namespace grapple::app {

std::string paramValueDisplayText(const timeline::ParamValue& value) {
  return std::visit(
    [](const auto& typedValue) -> std::string {
      using Value = std::decay_t<decltype(typedValue)>;
      std::ostringstream output;
      if constexpr (std::is_same_v<Value, double>) {
        output << typedValue;
      } else if constexpr (std::is_same_v<Value, bool>) {
        output << (typedValue ? "true" : "false");
      } else if constexpr (std::is_same_v<Value, std::string>) {
        output << typedValue;
      } else if constexpr (std::is_same_v<Value, foundation::Vec2>) {
        output << typedValue.x << ", " << typedValue.y;
      } else if constexpr (std::is_same_v<Value, foundation::Vec3>) {
        output << typedValue.x << ", " << typedValue.y << ", " << typedValue.z;
      } else if constexpr (std::is_same_v<Value, foundation::Rect>) {
        output << typedValue.x << ", " << typedValue.y << ", " << typedValue.width << "x" << typedValue.height;
      }
      return output.str();
    },
    value
  );
}

timeline::ParamValue sampledEffectParamValue(
  const AppEffectParamRow& param,
  foundation::TimeSeconds playhead
) {
  timeline::Param timelineParam{param.name, param.value};
  timelineParam.keyframes.reserve(param.keyframes.size());
  for (const AppEffectParamRow::Keyframe& keyframe : param.keyframes) {
    timelineParam.keyframes.push_back(timeline::Param::Keyframe{
      keyframe.keyframeId,
      keyframe.time,
      keyframe.value
    });
  }
  return timeline::sampleParamValue(timelineParam, playhead);
}

std::optional<foundation::NodeId> stewardCameraTargetId(
  const AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  if (selectedNodeId.has_value()) {
    for (const AppCameraRow& camera : viewModel.timeline.cameras) {
      if (camera.sourceNodeId == selectedNodeId.value()) {
        return camera.sourceNodeId;
      }
    }
  }

  if (viewModel.timeline.cameras.size() == 1) {
    return viewModel.timeline.cameras.front().sourceNodeId;
  }

  return std::nullopt;
}

bool cameraHasTransformEffect(
  const AppViewModel& viewModel,
  const foundation::NodeId& cameraNodeId
) {
  for (const AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
    if (graph.targetNodeId != cameraNodeId) {
      continue;
    }
    for (const AppEffectRow& effect : graph.effects) {
      if (effect.cameraTransformEffect) {
        return true;
      }
    }
  }
  return false;
}

} // namespace grapple::app
