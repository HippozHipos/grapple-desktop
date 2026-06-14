#include <grapple/jobs/JobScheduler.hpp>

#include <utility>

namespace grapple::jobs {

namespace {

foundation::Result<void> validateJob(const Job& job) {
  if (!job.id) {
    return foundation::Error{"jobs.job_id_empty", "Job id must not be empty."};
  }
  if (job.name.empty()) {
    return foundation::Error{"jobs.job_name_empty", "Job name must not be empty."};
  }
  if (!job.handler) {
    return foundation::Error{"jobs.job_handler_empty", "Job handler must not be empty."};
  }
  return {};
}

} // namespace

class JobScheduler::SchedulerProgressSink final : public IProgressSink {
public:
  SchedulerProgressSink(JobScheduler& scheduler, foundation::JobId jobId)
    : scheduler_{scheduler},
      jobId_{std::move(jobId)} {}

  void reportProgress(double progress) override {
    scheduler_.recordProgress(jobId_, progress);
  }

private:
  JobScheduler& scheduler_;
  foundation::JobId jobId_;
};

JobScheduler::JobScheduler()
  : worker_{[this] { workerLoop(); }} {}

JobScheduler::~JobScheduler() {
  {
    std::lock_guard lock{mutex_};
    stopping_ = true;
    for (ScheduledJob& scheduled : queue_) {
      scheduled.cancellation.cancel();
    }
    if (runningCancellation_.has_value()) {
      runningCancellation_->cancel();
    }
  }
  workAvailable_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

foundation::Result<void> JobScheduler::enqueue(Job job) {
  auto valid = validateJob(job);
  if (!valid) {
    return valid.error();
  }

  {
    std::lock_guard lock{mutex_};
    if (stopping_) {
      return foundation::Error{"jobs.scheduler_stopping", "Cannot enqueue a job after scheduler shutdown starts."};
    }
    queue_.push_back(ScheduledJob{std::move(job), CancellationToken{}});
  }
  workAvailable_.notify_one();
  return {};
}

void JobScheduler::cancel(foundation::JobId jobId) {
  std::lock_guard lock{mutex_};
  for (ScheduledJob& scheduled : queue_) {
    if (scheduled.job.id == jobId) {
      scheduled.cancellation.cancel();
    }
  }
  if (runningJobId_.has_value() && runningJobId_.value() == jobId && runningCancellation_.has_value()) {
    runningCancellation_->cancel();
  }
}

void JobScheduler::cancelAll() {
  std::lock_guard lock{mutex_};
  for (ScheduledJob& scheduled : queue_) {
    scheduled.cancellation.cancel();
  }
  if (runningCancellation_.has_value()) {
    runningCancellation_->cancel();
  }
}

void JobScheduler::waitUntilIdle() {
  std::unique_lock lock{mutex_};
  idle_.wait(lock, [&] {
    return queue_.empty() && !runningJobId_.has_value();
  });
}

std::size_t JobScheduler::queuedCount() const {
  std::lock_guard lock{mutex_};
  return queue_.size();
}

bool JobScheduler::running() const {
  std::lock_guard lock{mutex_};
  return runningJobId_.has_value();
}

std::vector<JobRunRecord> JobScheduler::runRecords() const {
  std::lock_guard lock{mutex_};
  return runRecords_;
}

std::vector<JobProgressRecord> JobScheduler::progressRecords() const {
  std::lock_guard lock{mutex_};
  return progressRecords_;
}

void JobScheduler::workerLoop() {
  while (true) {
    ScheduledJob scheduled;
    {
      std::unique_lock lock{mutex_};
      workAvailable_.wait(lock, [&] {
        return stopping_ || !queue_.empty();
      });
      if (stopping_ && queue_.empty()) {
        return;
      }

      scheduled = std::move(queue_.front());
      queue_.pop_front();
      runningJobId_ = scheduled.job.id;
      runningCancellation_ = scheduled.cancellation;
    }

    JobRunRecord record{scheduled.job.id};
    if (scheduled.cancellation.cancelled()) {
      record.status = JobRunStatus::Cancelled;
    } else {
      SchedulerProgressSink progress{*this, scheduled.job.id};
      auto result = scheduled.job.handler(scheduled.cancellation, progress);
      if (!result) {
        record.status = JobRunStatus::Failed;
        record.error = result.error();
      } else if (scheduled.cancellation.cancelled()) {
        record.status = JobRunStatus::Cancelled;
      } else {
        record.status = JobRunStatus::Succeeded;
      }
    }

    recordRun(std::move(record));
    {
      std::lock_guard lock{mutex_};
      runningJobId_.reset();
      runningCancellation_.reset();
    }
    idle_.notify_all();
  }
}

void JobScheduler::recordProgress(const foundation::JobId& jobId, double progress) {
  std::lock_guard lock{mutex_};
  progressRecords_.push_back(JobProgressRecord{jobId, progress});
}

void JobScheduler::recordRun(JobRunRecord record) {
  std::lock_guard lock{mutex_};
  runRecords_.push_back(std::move(record));
}

} // namespace grapple::jobs
