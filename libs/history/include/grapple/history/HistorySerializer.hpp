#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/history/CommandLogStore.hpp>
#include <grapple/history/EventLogStore.hpp>

#include <string>

namespace grapple::history {

std::string serializeCanonicalCommandLog(const CommandLogStore& log);
std::string serializeCanonicalEventLog(const EventLogStore& log);
foundation::Result<CommandLogStore> deserializeCanonicalCommandLog(const std::string& json);
foundation::Result<EventLogStore> deserializeCanonicalEventLog(const std::string& json);

} // namespace grapple::history
