#include <grapple/app/NativeProjectSession.hpp>

#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/ProjectPackageWriter.hpp>

#include <algorithm>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

std::size_t countClipsForLayer(
  const std::vector<projection::RenderClip>& clips,
  const foundation::NodeId& layerNodeId
) {
  return static_cast<std::size_t>(std::count_if(clips.begin(), clips.end(), [&](const projection::RenderClip& clip) {
    return clip.trackNodeId == layerNodeId;
  }));
}

} // namespace

NativeProjectSession::NativeProjectSession(
  foundation::ProjectId projectId,
  std::string projectName,
  storage::ProjectPackage package
) : NativeProjectSession{
      project::createEmptyProject(std::move(projectId), std::move(projectName)),
      std::move(package)
    } {}

NativeProjectSession::NativeProjectSession(project::ProjectDocument document, storage::ProjectPackage package)
  : session_{std::move(document), std::move(package)} {}

foundation::Result<storage::ProjectPackageSessionResult> NativeProjectSession::applyAndCommit(
  const project::ProjectCommandEnvelope& command,
  storage::ProjectCommitRecordOptions options
) {
  return session_.applyAndCommit(command, std::move(options));
}

foundation::Result<project::ProjectSnapshot> NativeProjectSession::snapshot() const {
  return session_.snapshot();
}

foundation::Result<project::ProjectQueryResult> NativeProjectSession::query(const project::ProjectQuery& query) const {
  auto snapshotResult = session_.snapshot();
  if (!snapshotResult) {
    return snapshotResult.error();
  }

  return std::visit(
    [&](const auto& typedQuery) -> foundation::Result<project::ProjectQueryResult> {
      using Query = std::decay_t<decltype(typedQuery)>;
      if constexpr (std::is_same_v<Query, project::GetProjectSnapshotQuery>) {
        return project::ProjectQueryResult{project::ProjectSnapshotResult{snapshotResult.value()}};
      } else if constexpr (std::is_same_v<Query, project::GetGraphQuery>) {
        return project::ProjectQueryResult{project::GraphResult{snapshotResult.value().graph}};
      }
    },
    query
  );
}

foundation::Result<AppViewModel> NativeProjectSession::buildViewModel() const {
  auto snapshotResult = session_.snapshot();
  if (!snapshotResult) {
    return snapshotResult.error();
  }

  auto planResult = buildRenderPlan();
  if (!planResult) {
    return planResult.error();
  }

  const project::ProjectSnapshot& snapshot = snapshotResult.value();
  const projection::RenderPlan& plan = planResult.value().plan;

  AppViewModel viewModel;
  viewModel.project = AppProjectSummary{
    snapshot.info.id,
    snapshot.info.name,
    snapshot.revision,
    snapshot.revisionNumber,
    snapshot.canonicalHash
  };
  viewModel.assets = AppAssetSummary{snapshot.assets.assets().size()};
  viewModel.timeline.duration = plan.duration;

  for (const projection::RenderLayer& layer : plan.layers) {
    viewModel.timeline.layers.push_back(AppLayerRow{
      layer.sourceNodeId,
      layer.name,
      countClipsForLayer(plan.clips, layer.sourceNodeId)
    });
  }

  for (const projection::RenderClip& clip : plan.clips) {
    viewModel.timeline.clips.push_back(AppClipRow{
      clip.sourceNodeId,
      clip.trackNodeId,
      clip.payload.assetId,
      clip.payload.timelineRange
    });
  }

  for (const projection::RenderCamera& camera : plan.cameras) {
    viewModel.timeline.cameras.push_back(AppCameraRow{
      camera.sourceNodeId,
      camera.name
    });
  }

  for (const projection::RenderEffectGraph& effectGraph : plan.effectGraphs) {
    viewModel.timeline.effectGraphs.push_back(AppEffectGraphRow{
      effectGraph.id,
      effectGraph.targetNodeId,
      effectGraph.nodes.size(),
      effectGraph.edges.size()
    });
  }

  return viewModel;
}

foundation::Result<projection::BuildTimelineIRResult> NativeProjectSession::buildTimelineIR() const {
  const projection::ProjectionQueryService projectionQueries{*this};
  return projectionQueries.buildCurrentTimelineIR();
}

foundation::Result<projection::BuildRenderPlanResult> NativeProjectSession::buildRenderPlan() const {
  const projection::ProjectionQueryService projectionQueries{*this};
  return projectionQueries.buildCurrentRenderPlan();
}

foundation::Result<NativePackageWriteResult> NativeProjectSession::writePackage() const {
  const storage::ProjectPackageState& state = session_.packageState();
  auto snapshotResult = session_.snapshot();
  if (!snapshotResult) {
    return snapshotResult.error();
  }

  auto manifestResult = storage::buildProjectPackageManifest(state);
  if (!manifestResult) {
    return manifestResult.error();
  }

  if (!manifestResult.value().latestSnapshot.has_value()) {
    return foundation::Error{"app.package_snapshot_missing", "Package save requires a snapshot record at the current project head."};
  }

  const storage::ProjectPackageSnapshotManifest& snapshotManifest = *manifestResult.value().latestSnapshot;
  if (snapshotManifest.revision != snapshotResult.value().revision) {
    return foundation::Error{"app.package_snapshot_not_current", "Package snapshot record must match the current project revision."};
  }

  const storage::ProjectPackageWriter writer;
  auto snapshotPath = writer.writeSnapshot(storage::ProjectSnapshotWriteRequest{
    state.package,
    snapshotResult.value(),
    storage::SnapshotCommitRecord{
      snapshotManifest.id,
      snapshotManifest.documentPath,
      snapshotManifest.label
    }
  });
  if (!snapshotPath) {
    return snapshotPath.error();
  }

  auto manifestPath = writer.writeManifest(manifestResult.value(), state.package);
  if (!manifestPath) {
    return manifestPath.error();
  }

  return NativePackageWriteResult{
    snapshotPath.value(),
    manifestPath.value()
  };
}

const storage::ProjectPackageState& NativeProjectSession::packageState() const noexcept {
  return session_.packageState();
}

} // namespace grapple::app
