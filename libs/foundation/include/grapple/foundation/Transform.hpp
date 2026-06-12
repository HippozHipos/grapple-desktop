#pragma once

#include <grapple/foundation/Geometry.hpp>

namespace grapple::foundation {

struct Transform2D {
  Vec2 position;
  Vec2 scale{1.0, 1.0};
  double rotationDegrees = 0.0;
  double opacity = 1.0;

  friend auto operator<=>(const Transform2D&, const Transform2D&) = default;
};

} // namespace grapple::foundation
