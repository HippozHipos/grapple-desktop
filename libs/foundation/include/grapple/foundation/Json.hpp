#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace grapple::foundation {

std::string jsonQuoted(std::string_view value);
void writeJsonStringProperty(std::ostream& stream, std::string_view key, std::string_view value);

} // namespace grapple::foundation
