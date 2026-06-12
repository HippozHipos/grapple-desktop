#pragma once

#include <grapple/project/ProjectCommand.hpp>

#include <string_view>

namespace grapple::project {

std::string_view serializedCommandName(CommandKind kind);
std::string_view serializedCommandSourceKind(CommandSourceKind kind);

} // namespace grapple::project
