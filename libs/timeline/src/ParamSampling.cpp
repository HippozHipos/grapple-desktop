#include <grapple/timeline/ParamSampling.hpp>
#include <grapple/foundation/KeyframeSampling.hpp>

#include <optional>

namespace grapple::timeline {

ParamValue sampleParamValue(const Param& param, foundation::TimeSeconds time) {
  return foundation::sampleKeyframedValue<Param::Keyframe, ParamValue>(
    param.value,
    param.keyframes,
    time,
    [](const ParamValue& left, const ParamValue& right, double ratio) -> std::optional<ParamValue> {
      const auto* leftNumber = std::get_if<double>(&left);
      const auto* rightNumber = std::get_if<double>(&right);
      if (leftNumber == nullptr || rightNumber == nullptr) {
        return std::nullopt;
      }

      return *leftNumber + ((*rightNumber - *leftNumber) * ratio);
    }
  );
}

} // namespace grapple::timeline
