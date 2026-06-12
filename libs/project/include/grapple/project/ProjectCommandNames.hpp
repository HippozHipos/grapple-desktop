#pragma once

#include <grapple/project/ProjectCommand.hpp>

#include <string_view>

namespace grapple::project {

std::string_view serializedCommandName(CommandKind kind);

} // namespace grapple::project

