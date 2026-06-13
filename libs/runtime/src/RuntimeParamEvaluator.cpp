#include <grapple/runtime/RuntimeParamEvaluator.hpp>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace grapple::runtime {

namespace {

RuntimeValue runtimeValueFromParamValue(const timeline::ParamValue& value) {
  return std::visit(
    [](const auto& typedValue) -> RuntimeValue {
      return RuntimeValue{typedValue};
    },
    value
  );
}

RuntimeValue sampledKeyframeValue(const RuntimeParam& param, foundation::TimeSeconds time) {
  if (param.keyframes.empty()) {
    return param.value;
  }

  std::vector<RuntimeParamKeyframe> keyframes = param.keyframes;
  std::sort(keyframes.begin(), keyframes.end(), [](const RuntimeParamKeyframe& left, const RuntimeParamKeyframe& right) {
    if (left.time != right.time) {
      return left.time < right.time;
    }
    return left.id < right.id;
  });

  if (time <= keyframes.front().time) {
    return keyframes.front().value;
  }
  if (time >= keyframes.back().time) {
    return keyframes.back().value;
  }

  for (std::size_t index = 1; index < keyframes.size(); ++index) {
    const RuntimeParamKeyframe& right = keyframes[index];
    if (time > right.time) {
      continue;
    }
    if (time == right.time) {
      return right.value;
    }

    const RuntimeParamKeyframe& left = keyframes[index - 1];
    const auto* leftNumber = std::get_if<double>(&left.value);
    const auto* rightNumber = std::get_if<double>(&right.value);
    if (leftNumber == nullptr || rightNumber == nullptr) {
      return left.value;
    }

    const double span = right.time.value - left.time.value;
    if (span <= 0.0) {
      return RuntimeValue{*rightNumber};
    }
    const double ratio = (time.value - left.time.value) / span;
    return RuntimeValue{*leftNumber + ((*rightNumber - *leftNumber) * ratio)};
  }

  return keyframes.back().value;
}

} // namespace

RuntimeParamSet runtimeParamsFromEffectNode(const projection::RenderEffectNode& node) {
  RuntimeParamSet params;
  params.reserve(node.payload.params.values.size());
  for (const timeline::Param& param : node.payload.params.values) {
    std::vector<RuntimeParamKeyframe> keyframes;
    keyframes.reserve(param.keyframes.size());
    for (const timeline::Param::Keyframe& keyframe : param.keyframes) {
      keyframes.push_back(RuntimeParamKeyframe{
        keyframe.id,
        keyframe.time,
        runtimeValueFromParamValue(keyframe.value)
      });
    }
    params.push_back(RuntimeParam{
      param.name,
      runtimeValueFromParamValue(param.value),
      std::move(keyframes)
    });
  }
  return params;
}

RuntimeValueMap evaluateRuntimeParams(
  const RuntimeParamSet& params,
  foundation::TimeSeconds time
) {
  RuntimeValueMap values;
  values.reserve(params.size());
  for (const RuntimeParam& param : params) {
    values.push_back(RuntimeNamedValue{
      param.name,
      sampledKeyframeValue(param, time)
    });
  }
  return values;
}

} // namespace grapple::runtime
