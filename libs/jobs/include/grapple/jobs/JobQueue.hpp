#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/jobs/Job.hpp>

#include <deque>
#include <optional>
#include <vector>

namespace grapple::jobs {

enum class JobRunStatus {
  Succeeded,
  Failed,
  Cancelled
};

struct JobRunRecord {
  foundation::JobId jobId;
  bool succeeded = false;
  JobRunStatus status = JobRunStatus::Failed;
  std::optional<foundation::Error> error;
};

class JobQueue {
public:
  foundation::Result<void> enqueue(Job job);
  foundation::Result<std::vector<JobRunRecord>> drain(
    CancellationToken& cancellation,
    IProgressSink& progress
  );

  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::deque<Job> jobs_;
};

} // namespace grapple::jobs
