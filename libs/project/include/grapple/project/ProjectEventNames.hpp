#pragma once

#include <grapple/project/ProjectEvents.hpp>

#include <string_view>

namespace grapple::project {

std::string_view serializedEventName(EventKind kind);

} // namespace grapple::project

