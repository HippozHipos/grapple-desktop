#pragma once

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace grapple::tests {

inline void require(bool condition, std::string_view expression, std::string_view file, int line) {
  if (condition) {
    return;
  }

  std::cerr << file << ':' << line << ": requirement failed: " << expression << '\n';
  std::exit(1);
}

} // namespace grapple::tests

#define GRAPPLE_REQUIRE(expression) \
  ::grapple::tests::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

