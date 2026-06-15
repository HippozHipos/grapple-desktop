#pragma once

#include <grapple/foundation/Time.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace grapple::foundation {

template <typename Keyframe, typename Value, typename Interpolate>
Value sampleKeyframedValue(
  const Value& baseValue,
  std::vector<Keyframe> keyframes,
  TimeSeconds time,
  Interpolate interpolate
) {
  if (keyframes.empty()) {
    return baseValue;
  }

  std::sort(keyframes.begin(), keyframes.end(), [](const Keyframe& left, const Keyframe& right) {
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
    const Keyframe& right = keyframes[index];
    if (time > right.time) {
      continue;
    }
    if (time == right.time) {
      return right.value;
    }

    const Keyframe& left = keyframes[index - 1];
    const double span = right.time.value - left.time.value;
    if (span <= 0.0) {
      return right.value;
    }

    const double ratio = (time.value - left.time.value) / span;
    const std::optional<Value> interpolated = interpolate(left.value, right.value, ratio);
    if (interpolated.has_value()) {
      return interpolated.value();
    }
    return left.value;
  }

  return keyframes.back().value;
}

} // namespace grapple::foundation
