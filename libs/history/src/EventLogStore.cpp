#include <grapple/history/EventLogStore.hpp>

#include <algorithm>

namespace grapple::history {

foundation::Result<void> EventLogStore::append(EventRecord record) {
  if (!record.id) {
    return foundation::Error{"history.event_id_empty", "Event record id must not be empty."};
  }

  const auto exists = std::any_of(records_.begin(), records_.end(), [&](const EventRecord& existing) {
    return existing.id == record.id;
  });
  if (exists) {
    return foundation::Error{"history.event_id_duplicate", "Event record id already exists."};
  }

  records_.push_back(std::move(record));
  return {};
}

const std::vector<EventRecord>& EventLogStore::records() const noexcept {
  return records_;
}

} // namespace grapple::history

