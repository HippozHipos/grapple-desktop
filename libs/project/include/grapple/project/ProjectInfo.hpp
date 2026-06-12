#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <string>

namespace grapple::project {

struct ProjectInfo {
  foundation::ProjectId id;
  std::string name;
};

} // namespace grapple::project
