#include <grapple/runtime/RuntimeParamEvaluator.hpp>
#include <grapple/foundation/KeyframeSampling.hpp>

#include <optional>
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
      foundation::sampleKeyframedValue<RuntimeParamKeyframe, RuntimeValue>(
        param.value,
        param.keyframes,
        time,
        [](const RuntimeValue& left, const RuntimeValue& right, double ratio) -> std::optional<RuntimeValue> {
          const auto* leftNumber = std::get_if<double>(&left);
          const auto* rightNumber = std::get_if<double>(&right);
          if (leftNumber == nullptr || rightNumber == nullptr) {
            return std::nullopt;
          }
          return RuntimeValue{*leftNumber + ((*rightNumber - *leftNumber) * ratio)};
        }
      )
    });
  }
  return values;
}

} // namespace grapple::runtime
