#include <grapple/jobs/JobScheduler.hpp>
#include <grapple/jobs/JobQueue.hpp>
#include <grapple/jobs/MainThreadDispatcher.hpp>
#include <grapple/jobs/ProjectCommandQueue.hpp>
#include <grapple/project/ProjectController.hpp>

#include <TestAssert.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {

class TestProgressSink final : public grapple::jobs::IProgressSink {
public:
  void reportProgress(double progress) override {
    lastProgress = progress;
  }

  double lastProgress = 0.0;
};

class ThreadStartGate {
public:
  void open() {
    {
      std::lock_guard lock{mutex_};
      open_ = true;
    }
    condition_.notify_all();
  }

  bool waitForOpen() {
    std::unique_lock lock{mutex_};
    return condition_.wait_for(lock, std::chrono::seconds{2}, [&] {
      return open_;
    });
  }

private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool open_ = false;
};

} // namespace

int main() {
  using namespace grapple;

  project::ProjectController project{
    project::createEmptyProject(foundation::ProjectId{"proj_jobs"}, "Jobs Project")
  };

  const auto initial = project.snapshot();
  GRAPPLE_REQUIRE(initial);

  jobs::ProjectCommandQueue commandQueue{project};
  auto enqueueComposition = commandQueue.enqueue(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_composition"},
    foundation::ProjectId{"proj_jobs"},
    initial.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(enqueueComposition);
  GRAPPLE_REQUIRE(commandQueue.size() == 1);

  const auto commandResults = commandQueue.drain();
  GRAPPLE_REQUIRE(commandResults);
  GRAPPLE_REQUIRE(commandResults.value().size() == 1);
  GRAPPLE_REQUIRE(commandResults.value()[0].afterRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(commandQueue.size() == 0);

  const auto afterDrain = project.snapshot();
  GRAPPLE_REQUIRE(afterDrain);
  GRAPPLE_REQUIRE(afterDrain.value().graph.nodes().size() == 1);

  jobs::ProjectCommandQueue failingCommandQueue{project};
  const auto enqueueTrack = failingCommandQueue.enqueue(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_track"},
    foundation::ProjectId{"proj_jobs"},
    afterDrain.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_track"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_track"},
      "Video",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(enqueueTrack);
  const auto enqueueStaleTrack = failingCommandQueue.enqueue(project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_stale_track"},
    foundation::ProjectId{"proj_jobs"},
    afterDrain.value().revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_stale_track"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_stale_track"},
      "Stale Video",
      timeline::TrackKind::Visual
    }
  });
  GRAPPLE_REQUIRE(enqueueStaleTrack);
  GRAPPLE_REQUIRE(failingCommandQueue.size() == 2);
  const auto failedDrain = failingCommandQueue.drain();
  GRAPPLE_REQUIRE(!failedDrain);
  GRAPPLE_REQUIRE(failedDrain.error().code == "project.expected_revision_mismatch");
  GRAPPLE_REQUIRE(failingCommandQueue.size() == 0);
  const auto afterFailedDrain = project.snapshot();
  GRAPPLE_REQUIRE(afterFailedDrain);
  GRAPPLE_REQUIRE(afterFailedDrain.value().graph.hasNode(foundation::NodeId{"node_track"}));
  GRAPPLE_REQUIRE(!afterFailedDrain.value().graph.hasNode(foundation::NodeId{"node_stale_track"}));
  const auto repeatedFailedDrain = failingCommandQueue.drain();
  GRAPPLE_REQUIRE(repeatedFailedDrain);
  GRAPPLE_REQUIRE(repeatedFailedDrain.value().empty());

  jobs::JobQueue jobs;
  bool jobRan = false;
  auto enqueueJob = jobs.enqueue(jobs::Job{
    foundation::JobId{"job_1"},
    "Test Job",
    [&](jobs::CancellationToken& cancellation, jobs::IProgressSink& progress) {
      GRAPPLE_REQUIRE(!cancellation.cancelled());
      progress.reportProgress(1.0);
      jobRan = true;
      return foundation::Result<void>{};
    }
  });
  GRAPPLE_REQUIRE(enqueueJob);

  TestProgressSink progress;
  jobs::CancellationToken cancellation;
  const auto jobResults = jobs.drain(cancellation, progress);
  GRAPPLE_REQUIRE(jobResults);
  GRAPPLE_REQUIRE(jobResults.value().size() == 1);
  GRAPPLE_REQUIRE(jobResults.value()[0].jobId == foundation::JobId{"job_1"});
  GRAPPLE_REQUIRE(jobResults.value()[0].succeeded);
  GRAPPLE_REQUIRE(jobResults.value()[0].status == jobs::JobRunStatus::Succeeded);
  GRAPPLE_REQUIRE(jobRan);
  GRAPPLE_REQUIRE(progress.lastProgress == 1.0);

  jobs::JobQueue cancelledJobs;
  bool cancelledJobRan = false;
  const auto enqueueCancelledJob = cancelledJobs.enqueue(jobs::Job{
    foundation::JobId{"job_cancelled"},
    "Cancelled Job",
    [&](jobs::CancellationToken& token, jobs::IProgressSink& progressSink) {
      cancelledJobRan = true;
      progressSink.reportProgress(0.25);
      return foundation::Result<void>{};
    }
  });
  GRAPPLE_REQUIRE(enqueueCancelledJob);
  jobs::CancellationToken cancelledToken;
  cancelledToken.cancel();
  const auto cancelledJobResults = cancelledJobs.drain(cancelledToken, progress);
  GRAPPLE_REQUIRE(cancelledJobResults);
  GRAPPLE_REQUIRE(cancelledJobResults.value().empty());
  GRAPPLE_REQUIRE(!cancelledJobRan);
  GRAPPLE_REQUIRE(cancelledJobs.size() == 1);

  jobs::JobQueue midDrainCancelledJobs;
  bool firstMidDrainJobRan = false;
  bool secondMidDrainJobRan = false;
  const auto enqueueFirstMidDrainJob = midDrainCancelledJobs.enqueue(jobs::Job{
    foundation::JobId{"job_cancel_during_first"},
    "Cancel During First Job",
    [&](jobs::CancellationToken& token, jobs::IProgressSink& progressSink) {
      firstMidDrainJobRan = true;
      progressSink.reportProgress(0.5);
      token.cancel();
      return foundation::Result<void>{};
    }
  });
  GRAPPLE_REQUIRE(enqueueFirstMidDrainJob);
  const auto enqueueSecondMidDrainJob = midDrainCancelledJobs.enqueue(jobs::Job{
    foundation::JobId{"job_after_cancel"},
    "Job After Cancel",
    [&](jobs::CancellationToken& token, jobs::IProgressSink& progressSink) {
      (void)token;
      secondMidDrainJobRan = true;
      progressSink.reportProgress(1.0);
      return foundation::Result<void>{};
    }
  });
  GRAPPLE_REQUIRE(enqueueSecondMidDrainJob);
  jobs::CancellationToken midDrainCancellation;
  const auto midDrainResults = midDrainCancelledJobs.drain(midDrainCancellation, progress);
  GRAPPLE_REQUIRE(midDrainResults);
  GRAPPLE_REQUIRE(midDrainResults.value().size() == 1);
  GRAPPLE_REQUIRE(midDrainResults.value()[0].jobId == foundation::JobId{"job_cancel_during_first"});
  GRAPPLE_REQUIRE(firstMidDrainJobRan);
  GRAPPLE_REQUIRE(!secondMidDrainJobRan);
  GRAPPLE_REQUIRE(midDrainCancelledJobs.size() == 1);
  GRAPPLE_REQUIRE(progress.lastProgress == 0.5);

  jobs::MainThreadDispatcher dispatcher;
  std::vector<int> dispatchedValues;
  dispatcher.post([&] { dispatchedValues.push_back(1); });
  dispatcher.post([&] { dispatchedValues.push_back(2); });
  GRAPPLE_REQUIRE(dispatcher.size() == 2);
  GRAPPLE_REQUIRE(dispatcher.drain() == 2);
  GRAPPLE_REQUIRE(dispatcher.size() == 0);
  GRAPPLE_REQUIRE(dispatchedValues.size() == 2);
  GRAPPLE_REQUIRE(dispatchedValues[0] == 1);
  GRAPPLE_REQUIRE(dispatchedValues[1] == 2);

  jobs::JobScheduler scheduler;
  const std::thread::id callerThread = std::this_thread::get_id();
  std::thread::id workerThread;
  const auto enqueueScheduledJob = scheduler.enqueue(jobs::Job{
    foundation::JobId{"job_scheduled"},
    "Scheduled Job",
    [&](jobs::CancellationToken& token, jobs::IProgressSink& progressSink) {
      GRAPPLE_REQUIRE(!token.cancelled());
      workerThread = std::this_thread::get_id();
      progressSink.reportProgress(0.25);
      progressSink.reportProgress(1.0);
      return foundation::Result<void>{};
    }
  });
  GRAPPLE_REQUIRE(enqueueScheduledJob);
  scheduler.waitUntilIdle();
  GRAPPLE_REQUIRE(workerThread != callerThread);
  const auto scheduledRecords = scheduler.runRecords();
  GRAPPLE_REQUIRE(scheduledRecords.size() == 1);
  GRAPPLE_REQUIRE(scheduledRecords[0].jobId == foundation::JobId{"job_scheduled"});
  GRAPPLE_REQUIRE(scheduledRecords[0].succeeded);
  GRAPPLE_REQUIRE(scheduledRecords[0].status == jobs::JobRunStatus::Succeeded);
  const auto scheduledProgress = scheduler.progressRecords();
  GRAPPLE_REQUIRE(scheduledProgress.size() == 2);
  GRAPPLE_REQUIRE(scheduledProgress[0].jobId == foundation::JobId{"job_scheduled"});
  GRAPPLE_REQUIRE(scheduledProgress[0].progress == 0.25);
  GRAPPLE_REQUIRE(scheduledProgress[1].progress == 1.0);

  jobs::JobScheduler cancellableScheduler;
  ThreadStartGate cancellationGate;
  std::atomic_bool cancellationObserved = false;
  const auto enqueueCancellableJob = cancellableScheduler.enqueue(jobs::Job{
    foundation::JobId{"job_cancellable"},
    "Cancellable Job",
    [&](jobs::CancellationToken& token, jobs::IProgressSink& progressSink) {
      progressSink.reportProgress(0.1);
      cancellationGate.open();
      while (!token.cancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
      cancellationObserved = true;
      return foundation::Result<void>{};
    }
  });
  GRAPPLE_REQUIRE(enqueueCancellableJob);
  GRAPPLE_REQUIRE(cancellationGate.waitForOpen());
  cancellableScheduler.cancel(foundation::JobId{"job_cancellable"});
  cancellableScheduler.waitUntilIdle();
  GRAPPLE_REQUIRE(cancellationObserved);
  const auto cancelledRecords = cancellableScheduler.runRecords();
  GRAPPLE_REQUIRE(cancelledRecords.size() == 1);
  GRAPPLE_REQUIRE(cancelledRecords[0].jobId == foundation::JobId{"job_cancellable"});
  GRAPPLE_REQUIRE(!cancelledRecords[0].succeeded);
  GRAPPLE_REQUIRE(cancelledRecords[0].status == jobs::JobRunStatus::Cancelled);
  const auto cancelledProgress = cancellableScheduler.progressRecords();
  GRAPPLE_REQUIRE(cancelledProgress.size() == 1);
  GRAPPLE_REQUIRE(cancelledProgress[0].jobId == foundation::JobId{"job_cancellable"});
  GRAPPLE_REQUIRE(cancelledProgress[0].progress == 0.1);

  return 0;
}
