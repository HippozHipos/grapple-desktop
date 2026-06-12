#pragma once

#include <compare>
#include <cstdint>

namespace grapple::foundation {

struct TimeSeconds {
  double value = 0.0;

  friend auto operator<=>(const TimeSeconds&, const TimeSeconds&) = default;
};

struct FrameNumber {
  std::int64_t value = 0;

  friend auto operator<=>(const FrameNumber&, const FrameNumber&) = default;
};

struct FrameRate {
  std::int32_t numerator = 24;
  std::int32_t denominator = 1;

  [[nodiscard]] double framesPerSecond() const noexcept {
    return static_cast<double>(numerator) / static_cast<double>(denominator);
  }

  friend auto operator<=>(const FrameRate&, const FrameRate&) = default;
};

struct TimeRange {
  TimeSeconds start;
  TimeSeconds end;

  [[nodiscard]] double duration() const noexcept {
    return end.value - start.value;
  }

  [[nodiscard]] bool contains(TimeSeconds time) const noexcept {
    return time.value >= start.value && time.value < end.value;
  }

  friend auto operator<=>(const TimeRange&, const TimeRange&) = default;
};

} // namespace grapple::foundation

