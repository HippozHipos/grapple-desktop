#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/jobs/Job.hpp>
#include <grapple/jobs/JobQueue.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace grapple::jobs {

struct JobProgressRecord {
  foundation::JobId jobId;
  double progress = 0.0;
};

class JobScheduler {
public:
  JobScheduler();
  ~JobScheduler();

  JobScheduler(const JobScheduler&) = delete;
  JobScheduler& operator=(const JobScheduler&) = delete;
  JobScheduler(JobScheduler&&) = delete;
  JobScheduler& operator=(JobScheduler&&) = delete;

  foundation::Result<void> enqueue(Job job);
  void cancel(foundation::JobId jobId);
  void cancelAll();
  void waitUntilIdle();

  [[nodiscard]] std::size_t queuedCount() const;
  [[nodiscard]] bool running() const;
  [[nodiscard]] std::vector<JobRunRecord> runRecords() const;
  [[nodiscard]] std::vector<JobProgressRecord> progressRecords() const;

private:
  class SchedulerProgressSink;

  struct ScheduledJob {
    Job job;
    CancellationToken cancellation;
  };

  void workerLoop();
  void recordProgress(const foundation::JobId& jobId, double progress);
  void recordRun(JobRunRecord record);

  mutable std::mutex mutex_;
  std::condition_variable workAvailable_;
  std::condition_variable idle_;
  std::deque<ScheduledJob> queue_;
  std::optional<foundation::JobId> runningJobId_;
  std::optional<CancellationToken> runningCancellation_;
  std::vector<JobRunRecord> runRecords_;
  std::vector<JobProgressRecord> progressRecords_;
  bool stopping_ = false;
  std::thread worker_;
};

} // namespace grapple::jobs
