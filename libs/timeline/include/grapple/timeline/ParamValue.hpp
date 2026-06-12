#pragma once

#include <grapple/foundation/Geometry.hpp>

#include <string>
#include <variant>

namespace grapple::timeline {

using ParamValue = std::variant<
  double,
  bool,
  std::string,
  foundation::Vec2,
  foundation::Vec3,
  foundation::Rect
>;

} // namespace grapple::timeline

