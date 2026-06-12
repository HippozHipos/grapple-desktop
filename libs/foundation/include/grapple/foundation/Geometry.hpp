#pragma once

#include <compare>

namespace grapple::foundation {

struct Vec2 {
  double x = 0.0;
  double y = 0.0;

  friend auto operator<=>(const Vec2&, const Vec2&) = default;
};

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  friend auto operator<=>(const Vec3&, const Vec3&) = default;
};

struct Rect {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;

  [[nodiscard]] Vec2 center() const noexcept {
    return Vec2{x + width * 0.5, y + height * 0.5};
  }

  friend auto operator<=>(const Rect&, const Rect&) = default;
};

struct Resolution {
  int width = 0;
  int height = 0;

  friend auto operator<=>(const Resolution&, const Resolution&) = default;
};

} // namespace grapple::foundation

