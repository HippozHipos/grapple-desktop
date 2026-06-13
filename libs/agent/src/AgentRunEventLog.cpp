#include <grapple/agent/AgentRunEventLog.hpp>

#include <algorithm>
#include <utility>

namespace grapple::agent {

foundation::Result<void> AgentRunEventLog::append(AgentRunEvent event) {
  if (!event.runId) {
    return foundation::Error{"agent.run_event_run_id_empty", "Agent run event requires a run id."};
  }
  if (event.sequence <= 0) {
    return foundation::Error{"agent.run_event_sequence_invalid", "Agent run event sequence must be positive."};
  }

  const auto duplicate = std::any_of(records_.begin(), records_.end(), [&](const AgentRunEvent& existing) {
    return existing.runId == event.runId && existing.sequence == event.sequence;
  });
  if (duplicate) {
    return foundation::Error{"agent.run_event_sequence_duplicate", "Agent run event sequence already exists for this run."};
  }

  const auto lastForRun = std::find_if(records_.rbegin(), records_.rend(), [&](const AgentRunEvent& existing) {
    return existing.runId == event.runId;
  });
  if (lastForRun != records_.rend() && event.sequence <= lastForRun->sequence) {
    return foundation::Error{"agent.run_event_sequence_not_ordered", "Agent run event sequence must increase for each run."};
  }

  records_.push_back(std::move(event));
  return {};
}

const std::vector<AgentRunEvent>& AgentRunEventLog::records() const noexcept {
  return records_;
}

} // namespace grapple::agent
