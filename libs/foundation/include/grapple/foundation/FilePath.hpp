#pragma once

#include <compare>
#include <string>

namespace grapple::foundation {

struct FilePath {
  std::string value;

  friend auto operator<=>(const FilePath&, const FilePath&) = default;
};

} // namespace grapple::foundation

