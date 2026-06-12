#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/history/CommandRecord.hpp>

#include <vector>

namespace grapple::history {

class CommandLogStore {
public:
  foundation::Result<void> append(CommandRecord record);
  [[nodiscard]] const std::vector<CommandRecord>& records() const noexcept;

private:
  std::vector<CommandRecord> records_;
};

} // namespace grapple::history

