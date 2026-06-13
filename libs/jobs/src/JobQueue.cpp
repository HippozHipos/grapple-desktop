#include <grapple/jobs/JobQueue.hpp>

namespace grapple::jobs {

foundation::Result<void> JobQueue::enqueue(Job job) {
  if (!job.id) {
    return foundation::Error{"jobs.job_id_empty", "Job id must not be empty."};
  }

  if (job.name.empty()) {
    return foundation::Error{"jobs.job_name_empty", "Job name must not be empty."};
  }

  if (!job.handler) {
    return foundation::Error{"jobs.job_handler_empty", "Job handler must not be empty."};
  }

  jobs_.push_back(std::move(job));
  return {};
}

foundation::Result<std::vector<JobRunRecord>> JobQueue::drain(
  CancellationToken& cancellation,
  IProgressSink& progress
) {
  std::vector<JobRunRecord> records;

  while (!jobs_.empty()) {
    if (cancellation.cancelled()) {
      break;
    }

    Job job = std::move(jobs_.front());
    jobs_.pop_front();

    auto result = job.handler(cancellation, progress);
    if (!result) {
      return result.error();
    }

    records.push_back(JobRunRecord{job.id, true});
  }

  return records;
}

std::size_t JobQueue::size() const noexcept {
  return jobs_.size();
}

} // namespace grapple::jobs
