#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectCommandResult.hpp>
#include <grapple/project/ProjectCommandService.hpp>

#include <deque>
#include <vector>

namespace grapple::jobs {

class ProjectCommandQueue {
public:
  explicit ProjectCommandQueue(project::IProjectCommandService& commands);

  foundation::Result<void> enqueue(project::ProjectCommandEnvelope command);
  foundation::Result<std::vector<project::ProjectCommandResult>> drain();

  [[nodiscard]] std::size_t size() const noexcept;

private:
  project::IProjectCommandService& commands_;
  std::deque<project::ProjectCommandEnvelope> queue_;
};

} // namespace grapple::jobs

