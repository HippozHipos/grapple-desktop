#include <grapple/jobs/ProjectCommandQueue.hpp>

#include <utility>

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
    project::ProjectCommandEnvelope command = std::move(queue_.front());
    queue_.pop_front();

    auto result = commands_.apply(command);
    if (!result) {
      return result.error();
    }

    results.push_back(std::move(result.value()));
  }

  return results;
}

std::size_t ProjectCommandQueue::size() const noexcept {
  return queue_.size();
}

} // namespace grapple::jobs
