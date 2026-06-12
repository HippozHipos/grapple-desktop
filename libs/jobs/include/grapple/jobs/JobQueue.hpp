#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/jobs/Job.hpp>

#include <deque>
#include <vector>

namespace grapple::jobs {

struct JobRunRecord {
  foundation::JobId jobId;
  bool succeeded = false;
};

class JobQueue {
public:
  foundation::Result<void> enqueue(Job job);
  foundation::Result<std::vector<JobRunRecord>> drain(IProgressSink& progress);

  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::deque<Job> jobs_;
};

} // namespace grapple::jobs

