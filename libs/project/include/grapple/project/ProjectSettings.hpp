#pragma once

#include <grapple/foundation/Time.hpp>

#include <optional>

namespace grapple::project {

struct ProjectSettings {
  std::optional<foundation::TimeSeconds> defaultDuration;
};

} // namespace grapple::project
