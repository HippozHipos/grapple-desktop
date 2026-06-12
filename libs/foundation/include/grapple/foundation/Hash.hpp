#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace grapple::foundation {

class Hash256 {
public:
  using Bytes = std::array<std::uint8_t, 32>;

  Hash256() = default;
  explicit Hash256(Bytes bytes);

  [[nodiscard]] const Bytes& bytes() const noexcept;
  [[nodiscard]] std::string toHex() const;

  friend bool operator==(const Hash256&, const Hash256&) = default;

private:
  Bytes bytes_{};
};

[[nodiscard]] Hash256 stableHash(std::string_view value);

} // namespace grapple::foundation

