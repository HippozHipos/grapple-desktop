#include <grapple/history/CommandLogStore.hpp>

#include <algorithm>

namespace grapple::history {

foundation::Result<void> CommandLogStore::append(CommandRecord record) {
  if (!record.id) {
    return foundation::Error{"history.command_id_empty", "Command record id must not be empty."};
  }

  const auto exists = std::any_of(records_.begin(), records_.end(), [&](const CommandRecord& existing) {
    return existing.id == record.id;
  });
  if (exists) {
    return foundation::Error{"history.command_id_duplicate", "Command record id already exists."};
  }

  records_.push_back(std::move(record));
  return {};
}

const std::vector<CommandRecord>& CommandLogStore::records() const noexcept {
  return records_;
}

} // namespace grapple::history

