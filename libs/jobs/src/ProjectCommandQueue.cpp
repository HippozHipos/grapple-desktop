#include <grapple/jobs/ProjectCommandQueue.hpp>

namespace grapple::jobs {

ProjectCommandQueue::ProjectCommandQueue(project::IProjectCommandService& commands)
  : commands_(commands) {}

foundation::Result<void> ProjectCommandQueue::enqueue(project::ProjectCommandEnvelope command) {
  if (!command.id) {
    return foundation::Error{"jobs.command_id_empty", "Queued project command id must not be empty."};
  }

  queue_.push_back(std::move(command));
  return {};
}

foundation::Result<std::vector<project::ProjectCommandResult>> ProjectCommandQueue::drain() {
  std::vector<project::ProjectCommandResult> results;

  while (!queue_.empty()) {
    auto result = commands_.apply(queue_.front());
    if (!result) {
      return result.error();
    }

    results.push_back(std::move(result.value()));
    queue_.pop_front();
  }

  return results;
}

std::size_t ProjectCommandQueue::size() const noexcept {
  return queue_.size();
}

} // namespace grapple::jobs
