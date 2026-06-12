#pragma once

#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Transform.hpp>
#include <grapple/media/AudioBuffer.hpp>
#include <grapple/media/MediaFrame.hpp>

#include <string>
#include <variant>

namespace grapple::runtime {

using RuntimeValue = std::variant<
  std::monostate,
  double,
  bool,
  std::string,
  foundation::Vec2,
  foundation::Vec3,
  foundation::Rect,
  foundation::Transform2D,
  media::MediaFrame,
  media::AudioBuffer
>;

} // namespace grapple::runtime
