#include <grapple/jobs/JobQueue.hpp>
#include <grapple/jobs/ProjectCommandQueue.hpp>
#include <grapple/project/ProjectController.hpp>

#include <TestAssert.hpp>

namespace {

class TestProgressSink final : public grapple::jobs::IProgressSink {
public:
  void reportProgress(double progress) override {
    lastProgress = progress;
  }

  double lastProgress = 0.0;
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
    project::CommandKind::CreateComposition,
    foundation::ProjectId{"proj_jobs"},
    initial.value().document.revision,
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
  GRAPPLE_REQUIRE(afterDrain.value().document.graph.nodes().size() == 1);

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
  const auto jobResults = jobs.drain(progress);
  GRAPPLE_REQUIRE(jobResults);
  GRAPPLE_REQUIRE(jobResults.value().size() == 1);
  GRAPPLE_REQUIRE(jobResults.value()[0].jobId == foundation::JobId{"job_1"});
  GRAPPLE_REQUIRE(jobResults.value()[0].succeeded);
  GRAPPLE_REQUIRE(jobRan);
  GRAPPLE_REQUIRE(progress.lastProgress == 1.0);

  return 0;
}

