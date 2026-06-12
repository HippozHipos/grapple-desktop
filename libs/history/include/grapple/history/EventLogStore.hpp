#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/history/EventRecord.hpp>

#include <vector>

namespace grapple::history {

class EventLogStore {
public:
  foundation::Result<void> append(EventRecord record);
  [[nodiscard]] const std::vector<EventRecord>& records() const noexcept;

private:
  std::vector<EventRecord> records_;
};

} // namespace grapple::history

