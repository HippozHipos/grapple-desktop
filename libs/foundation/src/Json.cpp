#include <grapple/foundation/Json.hpp>

#include <ostream>
#include <sstream>

namespace grapple::foundation {

std::string jsonQuoted(std::string_view value) {
  std::ostringstream stream;
  stream << '"';
  for (const char character : value) {
    if (character == '\\') {
      stream << "\\\\";
    } else if (character == '"') {
      stream << "\\\"";
    } else if (character == '\n') {
      stream << "\\n";
    } else {
      stream << character;
    }
  }
  stream << '"';
  return stream.str();
}

void writeJsonStringProperty(std::ostream& stream, std::string_view key, std::string_view value) {
  stream << jsonQuoted(key) << ':' << jsonQuoted(value);
}

} // namespace grapple::foundation
