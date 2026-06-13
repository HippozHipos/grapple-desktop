#include <grapple/app/NativeProjectSession.hpp>

#include <grapple/asset/Asset.hpp>
#include <grapple/history/CommandRecord.hpp>
#include <grapple/history/SnapshotRecord.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/ProjectPackageWriter.hpp>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

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

std::string mediaTypeName(asset::AssetMediaType mediaType) {
  switch (mediaType) {
    case asset::AssetMediaType::Video:
      return "video";
    case asset::AssetMediaType::Audio:
      return "audio";
    case asset::AssetMediaType::Image:
      return "image";
  }

  std::abort();
}

std::string implementationKindName(timeline::EffectImplementationKind kind) {
  switch (kind) {
    case timeline::EffectImplementationKind::Builtin:
      return "builtin";
    case timeline::EffectImplementationKind::Python:
      return "python";
    case timeline::EffectImplementationKind::Shader:
      return "shader";
  }

  std::abort();
}

std::string paramValueText(const timeline::ParamValue& value) {
  return std::visit(
    [](const auto& typedValue) -> std::string {
      using Value = std::decay_t<decltype(typedValue)>;
      std::ostringstream output;
      if constexpr (std::is_same_v<Value, double>) {
        output << typedValue;
      } else if constexpr (std::is_same_v<Value, bool>) {
        output << (typedValue ? "true" : "false");
      } else if constexpr (std::is_same_v<Value, std::string>) {
        output << typedValue;
      } else if constexpr (std::is_same_v<Value, foundation::Vec2>) {
        output << typedValue.x << ", " << typedValue.y;
      } else if constexpr (std::is_same_v<Value, foundation::Vec3>) {
        output << typedValue.x << ", " << typedValue.y << ", " << typedValue.z;
      } else if constexpr (std::is_same_v<Value, foundation::Rect>) {
        output << typedValue.x << ", " << typedValue.y << ", " << typedValue.width << "x" << typedValue.height;
      }
      return output.str();
    },
    value
  );
}

std::optional<std::string> snapshotLabelForRevision(
  const std::vector<history::SnapshotRecord>& snapshots,
  const foundation::RevisionId& revision
) {
  const auto snapshot = std::find_if(snapshots.begin(), snapshots.end(), [&](const history::SnapshotRecord& record) {
    return record.revision == revision;
  });
  if (snapshot == snapshots.end()) {
    return std::nullopt;
  }
  return snapshot->label;
}

project::RenderPlanInspectResult inspectRenderPlan(const projection::RenderPlan& plan) {
  project::RenderPlanInspectResult result{
    plan.projectId,
    plan.revision,
    plan.duration,
    plan.assets.size(),
    {},
    {},
    {},
    {},
    plan.diagnostics.size()
  };

  for (const projection::RenderLayer& layer : plan.layers) {
    result.layers.push_back(project::RenderPlanLayerSummary{
      layer.sourceNodeId,
      layer.name
    });
  }

  for (const projection::RenderClip& clip : plan.clips) {
    result.clips.push_back(project::RenderPlanClipSummary{
      clip.sourceNodeId,
      clip.trackNodeId,
      clip.payload.assetId,
      clip.payload.kind,
      clip.payload.timelineRange
    });
  }

  for (const projection::RenderCamera& camera : plan.cameras) {
    result.cameras.push_back(project::RenderPlanCameraSummary{
      camera.sourceNodeId,
      camera.name
    });
  }

  for (const projection::RenderEffectGraph& effectGraph : plan.effectGraphs) {
    result.effectGraphs.push_back(project::RenderPlanEffectGraphSummary{
      effectGraph.id,
      effectGraph.targetNodeId,
      effectGraph.nodes.size(),
      effectGraph.edges.size()
    });
  }

  return result;
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

NativeProjectSession::NativeProjectSession(storage::ProjectPackageSession session)
  : session_{std::move(session)} {}

foundation::Result<NativeProjectSession> NativeProjectSession::openPackage(storage::ProjectPackage package) {
  auto session = storage::ProjectPackageSession::open(std::move(package));
  if (!session) {
    return session.error();
  }
  return NativeProjectSession{std::move(session.value())};
}

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
      } else if constexpr (std::is_same_v<Query, project::InspectCompositionsQuery>) {
        auto result = project::inspectCompositions(snapshotResult.value());
        if (!result) {
          return result.error();
        }
        return project::ProjectQueryResult{result.value()};
      } else if constexpr (std::is_same_v<Query, project::InspectRenderPlanQuery>) {
        const projection::ProjectionQueryService projectionQueries{*this};
        auto result = projectionQueries.buildCurrentRenderPlan();
        if (!result) {
          return result.error();
        }
        return project::ProjectQueryResult{inspectRenderPlan(result.value().plan)};
      } else if constexpr (std::is_same_v<Query, project::InspectRuntimeDiagnosticsQuery>) {
        return foundation::Error{
          "app.runtime_diagnostics_query_requires_workspace",
          "Runtime diagnostic inspection requires a workspace query service with runtime configuration."
        };
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
  const storage::ProjectPackageState& packageState = session_.packageState();
  for (const history::CommandRecord& command : packageState.commandLog.records()) {
    if (command.sourceKind != "agent" || command.sourceActorName != "steward") {
      continue;
    }
    const std::optional<std::string> intent = snapshotLabelForRevision(
      packageState.snapshots.records(),
      command.afterRevision
    );
    if (!intent.has_value()) {
      continue;
    }
    viewModel.steward.edits.push_back(AppStewardEditRow{
      command.id,
      command.afterRevision,
      intent.value()
    });
  }
  viewModel.assets.count = snapshot.assets.assets().size();
  viewModel.timeline.duration = plan.duration;

  for (const asset::Asset& asset : snapshot.assets.assets()) {
    viewModel.assets.rows.push_back(AppAssetRow{
      asset.id,
      asset.name,
      mediaTypeName(asset.metadata.mediaType),
      asset.metadata.sourcePath,
      asset.metadata.duration,
      asset.metadata.dimensions
    });
  }

  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    if (node.kind == graph::NodeKind::Composition) {
      const auto* payload = std::get_if<timeline::CompositionPayload>(&node.payload);
      if (payload != nullptr) {
        viewModel.timeline.compositions.push_back(AppCompositionRow{
          node.id,
          payload->name
        });
      }
    }
  }

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
    AppEffectGraphRow effectGraphRow{
      effectGraph.id,
      effectGraph.targetNodeId,
      effectGraph.nodes.size(),
      effectGraph.edges.size(),
      {}
    };

    for (const projection::RenderEffectNode& effect : effectGraph.nodes) {
      std::vector<AppEffectParamRow> params;
      params.reserve(effect.payload.params.values.size());
      for (const timeline::Param& param : effect.payload.params.values) {
        params.push_back(AppEffectParamRow{
          param.name,
          param.control.label,
          paramValueText(param.value),
          param.control.numeric.has_value() ? std::optional<double>{param.control.numeric->min} : std::nullopt,
          param.control.numeric.has_value() ? std::optional<double>{param.control.numeric->max} : std::nullopt,
          param.control.numeric.has_value() ? param.control.numeric->step : std::nullopt
        });
      }

      effectGraphRow.effects.push_back(AppEffectRow{
        effectGraph.id,
        effect.sourceNodeId,
        effectGraph.targetNodeId,
        effect.payload.displayName,
        implementationKindName(effect.payload.implementation.kind),
        effect.payload.implementation.entrypoint,
        effect.payload.activeRange,
        std::move(params)
      });
    }

    viewModel.timeline.effectGraphs.push_back(std::move(effectGraphRow));
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

  auto commandLogPath = writer.writeCommandLog(storage::ProjectCommandLogWriteRequest{
    state.package,
    manifestResult.value().commandLogPath,
    state.commandLog
  });
  if (!commandLogPath) {
    return commandLogPath.error();
  }

  auto eventLogPath = writer.writeEventLog(storage::ProjectEventLogWriteRequest{
    state.package,
    manifestResult.value().eventLogPath,
    state.eventLog
  });
  if (!eventLogPath) {
    return eventLogPath.error();
  }

  auto manifestPath = writer.writeManifest(manifestResult.value(), state.package);
  if (!manifestPath) {
    return manifestPath.error();
  }

  return NativePackageWriteResult{
    snapshotPath.value(),
    manifestPath.value(),
    commandLogPath.value(),
    eventLogPath.value()
  };
}

const storage::ProjectPackageState& NativeProjectSession::packageState() const noexcept {
  return session_.packageState();
}

} // namespace grapple::app
