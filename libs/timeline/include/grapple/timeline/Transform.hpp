#pragma once

#include <grapple/foundation/Geometry.hpp>

namespace grapple::timeline {

struct Transform {
  foundation::Vec2 position;
  foundation::Vec2 scale{1.0, 1.0};
  double rotationDegrees = 0.0;
  double opacity = 1.0;

  friend auto operator<=>(const Transform&, const Transform&) = default;
};

} // namespace grapple::timeline

