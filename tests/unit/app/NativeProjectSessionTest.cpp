#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>

#include <TestAssert.hpp>

#include <chrono>
#include <variant>

int main() {
  using namespace grapple;

  app::NativeProjectSession session{
    foundation::ProjectId{"proj_app"},
    "App Project",
    storage::ProjectPackage{
      foundation::ProjectId{"proj_app"},
      foundation::FilePath{"app.grapple"},
      1
    }
  };

  const auto initial = session.snapshot();
  GRAPPLE_REQUIRE(initial);

  const auto composition = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_composition"},
      foundation::ProjectId{"proj_app"},
      initial.value().revision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
      project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
    },
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      std::nullopt
    }
  );
  GRAPPLE_REQUIRE(composition);
  GRAPPLE_REQUIRE(composition.value().snapshot.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().head.has_value());
  GRAPPLE_REQUIRE(session.packageState().head->currentRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == 1);

  const auto snapshotQuery = session.query(project::GetProjectSnapshotQuery{});
  GRAPPLE_REQUIRE(snapshotQuery);
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&snapshotQuery.value());
  GRAPPLE_REQUIRE(snapshotResult != nullptr);
  GRAPPLE_REQUIRE(snapshotResult->snapshot.revision == foundation::RevisionId{"rev_1"});

  const auto graphQuery = session.query(project::GetGraphQuery{});
  GRAPPLE_REQUIRE(graphQuery);
  const auto* graphResult = std::get_if<project::GraphResult>(&graphQuery.value());
  GRAPPLE_REQUIRE(graphResult != nullptr);
  GRAPPLE_REQUIRE(graphResult->graph.nodes().size() == 1);

  const auto timeline = session.buildTimelineIR();
  GRAPPLE_REQUIRE(timeline);
  GRAPPLE_REQUIRE(timeline.value().timeline.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(timeline.value().timeline.layers.empty());

  const auto renderPlan = session.buildRenderPlan();
  GRAPPLE_REQUIRE(renderPlan);
  GRAPPLE_REQUIRE(renderPlan.value().plan.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(renderPlan.value().plan.effectGraphs.empty());

  const auto viewModel = session.buildViewModel();
  GRAPPLE_REQUIRE(viewModel);
  GRAPPLE_REQUIRE(viewModel.value().project.projectId == foundation::ProjectId{"proj_app"});
  GRAPPLE_REQUIRE(viewModel.value().project.name == "App Project");
  GRAPPLE_REQUIRE(viewModel.value().project.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(viewModel.value().project.revisionNumber == 1);
  GRAPPLE_REQUIRE(viewModel.value().assets.count == 0);
  GRAPPLE_REQUIRE(viewModel.value().timeline.duration == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(viewModel.value().timeline.layers.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.clips.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.cameras.empty());
  GRAPPLE_REQUIRE(viewModel.value().timeline.effectGraphs.empty());

  const auto manifest = storage::buildProjectPackageManifest(session.packageState());
  GRAPPLE_REQUIRE(manifest);
  GRAPPLE_REQUIRE(manifest.value().head.has_value());
  GRAPPLE_REQUIRE(manifest.value().head->lastCommandId == foundation::CommandId{"cmd_composition"});
  GRAPPLE_REQUIRE(!manifest.value().latestSnapshot.has_value());

  app::NativePreviewSession preview{session};
  const auto frameBeforeRefresh = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(!frameBeforeRefresh);
  GRAPPLE_REQUIRE(frameBeforeRefresh.error().code == "render.plan_missing");

  const auto refresh = preview.refreshFromProject();
  GRAPPLE_REQUIRE(refresh);
  GRAPPLE_REQUIRE(refresh.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(preview.state().core.hasPlan);
  GRAPPLE_REQUIRE(preview.state().core.preparedPlanHash == refresh.value().preparedPlanHash);

  const auto seek = preview.seek(foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(seek);
  const auto frame = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(frame);
  GRAPPLE_REQUIRE(frame.value().frame.description == "layers=0 clips=0 cameras=0 effects=0");
  GRAPPLE_REQUIRE(frame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(frame.value().renderDiagnostics.empty());

  const auto duplicate = session.applyAndCommit(
    project::ProjectCommandEnvelope{
      foundation::CommandId{"cmd_composition"},
      foundation::ProjectId{"proj_app"},
      composition.value().snapshot.revision,
      project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
      project::CreateCompositionCommand{foundation::NodeId{"node_other"}, "Other"}
    },
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      std::nullopt
    }
  );
  GRAPPLE_REQUIRE(!duplicate);
  GRAPPLE_REQUIRE(duplicate.error().code == "history.command_id_duplicate");
  const auto afterDuplicate = session.snapshot();
  GRAPPLE_REQUIRE(afterDuplicate);
  GRAPPLE_REQUIRE(afterDuplicate.value().revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(session.packageState().commandLog.records().size() == 1);

  return 0;
}
