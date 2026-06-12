#include <grapple/foundation/Hash.hpp>

#include <array>
#include <iomanip>
#include <sstream>

namespace grapple::foundation {

namespace {

std::uint64_t fnv1a64(std::string_view value, std::uint64_t seed) {
  std::uint64_t hash = 14695981039346656037ull ^ seed;
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

void write64(Hash256::Bytes& bytes, std::size_t offset, std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xffu);
  }
}

} // namespace

Hash256::Hash256(Bytes bytes)
  : bytes_(bytes) {}

const Hash256::Bytes& Hash256::bytes() const noexcept {
  return bytes_;
}

std::string Hash256::toHex() const {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const std::uint8_t byte : bytes_) {
    stream << std::setw(2) << static_cast<int>(byte);
  }
  return stream.str();
}

Hash256 stableHash(std::string_view value) {
  Hash256::Bytes bytes{};
  write64(bytes, 0, fnv1a64(value, 0x00ull));
  write64(bytes, 8, fnv1a64(value, 0x9e3779b97f4a7c15ull));
  write64(bytes, 16, fnv1a64(value, 0xc2b2ae3d27d4eb4full));
  write64(bytes, 24, fnv1a64(value, 0x165667b19e3779f9ull));
  return Hash256{bytes};
}

} // namespace grapple::foundation

