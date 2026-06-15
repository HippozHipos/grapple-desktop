#include <grapple/app/NativeProjectSession.hpp>

#include <grapple/asset/Asset.hpp>
#include <grapple/history/CommandRecord.hpp>
#include <grapple/effects/BuiltinEffects.hpp>
#include <grapple/history/SnapshotRecord.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/ProjectPackageWriter.hpp>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace grapple::app {

namespace {

template <typename Clip>
std::size_t countClipsForLayer(
  const std::vector<Clip>& clips,
  const foundation::NodeId& layerNodeId
) {
  return static_cast<std::size_t>(std::count_if(clips.begin(), clips.end(), [&](const Clip& clip) {
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

std::string clipKindName(timeline::ClipKind kind) {
  switch (kind) {
    case timeline::ClipKind::Video:
      return "video";
    case timeline::ClipKind::Audio:
      return "audio";
    case timeline::ClipKind::Image:
      return "image";
  }

  std::abort();
}

foundation::Result<std::string> assetNameFor(
  const asset::AssetCatalog& assets,
  const foundation::AssetId& assetId
) {
  const asset::Asset* asset = assets.find(assetId);
  if (asset == nullptr) {
    return foundation::Error{"app.clip_asset_missing", "Clip asset must exist in the project asset catalog."};
  }
  return asset->name;
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

foundation::Result<foundation::RevisionId> currentContentRevision(
  const std::vector<history::CommandRecord>& commands,
  const project::ProjectSnapshot& snapshot
) {
  const auto headCommand = std::find_if(commands.rbegin(), commands.rend(), [&](const history::CommandRecord& command) {
    return command.afterRevision == snapshot.revision;
  });
  if (headCommand == commands.rend() || headCommand->serializedName != "project.restore_snapshot") {
    return snapshot.revision;
  }

  auto parsedCommand = project::deserializeCanonicalCommandPayload(
    headCommand->serializedName,
    headCommand->serializedPayload
  );
  if (!parsedCommand) {
    return parsedCommand.error();
  }

  const auto* restoredSnapshot = std::get_if<project::RestoreSnapshotCommand>(&parsedCommand.value());
  if (restoredSnapshot == nullptr) {
    return foundation::Error{
      "app.restore_payload_invalid",
      "Restore command payload must contain a restored snapshot."
    };
  }
  return restoredSnapshot->snapshot.revision;
}

foundation::Result<std::string> nodeDisplayName(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& nodeId
) {
  const graph::GraphNode* node = snapshot.graph.findNode(nodeId);
  if (node == nullptr) {
    return nodeId.value();
  }

  if (const auto* camera = std::get_if<timeline::CameraPayload>(&node->payload)) {
    return camera->name;
  }
  if (const auto* track = std::get_if<timeline::TrackPayload>(&node->payload)) {
    return track->name;
  }
  if (const auto* composition = std::get_if<timeline::CompositionPayload>(&node->payload)) {
    return composition->name;
  }
  if (const auto* clip = std::get_if<timeline::ClipPayload>(&node->payload)) {
    auto assetName = assetNameFor(snapshot.assets, clip->assetId);
    if (!assetName) {
      return assetName.error();
    }
    return assetName.value();
  }

  return nodeId.value();
}

struct EffectCreationProvenance {
  foundation::CommandId commandId;
  foundation::NodeId effectNodeId;
  foundation::NodeId targetNodeId;
  foundation::RevisionId revision;
  std::string sourceKind;
  std::string sourceActorName;
  std::string effectName;
  std::string intent;
};

struct EffectParamEditProvenance {
  foundation::NodeId effectNodeId;
  std::string paramName;
  foundation::RevisionId revision;
  std::string sourceKind;
  std::string sourceActorName;
};

struct EffectKeyframeEditProvenance {
  foundation::NodeId effectNodeId;
  std::string paramName;
  foundation::KeyframeId keyframeId;
  foundation::RevisionId revision;
  std::string sourceKind;
  std::string sourceActorName;
};

struct EffectCommandProvenance {
  std::vector<EffectCreationProvenance> creations;
  std::vector<EffectParamEditProvenance> paramEdits;
  std::vector<EffectKeyframeEditProvenance> keyframeEdits;
};

struct AppCommandProvenance {
  EffectCommandProvenance effects;
  std::vector<AppStewardEditRow> stewardEdits;
};

foundation::Result<AppCommandProvenance> appCommandProvenance(
  const std::vector<history::CommandRecord>& commands,
  const std::vector<history::SnapshotRecord>& snapshots,
  const project::ProjectSnapshot& snapshot,
  const foundation::RevisionId& contentRevision
) {
  AppCommandProvenance provenance;
  if (contentRevision == foundation::RevisionId{"rev_0"}) {
    return provenance;
  }

  bool reachedContentRevision = false;
  for (const history::CommandRecord& command : commands) {
    auto parsedCommand = project::deserializeCanonicalCommandPayload(
      command.serializedName,
      command.serializedPayload
    );
    if (!parsedCommand) {
      return parsedCommand.error();
    }

    const bool stewardCommand = command.sourceKind == "agent" && command.sourceActorName == "steward";
    const std::string intent = stewardCommand
      ? snapshotLabelForRevision(snapshots, command.afterRevision).value_or(std::string{})
      : std::string{};

    if (const auto* createEffect = std::get_if<project::CreateEffectCommand>(&parsedCommand.value())) {
      provenance.effects.creations.push_back(EffectCreationProvenance{
        command.id,
        createEffect->nodeId,
        createEffect->targetNodeId,
        command.afterRevision,
        command.sourceKind,
        command.sourceActorName,
        createEffect->payload.displayName,
        snapshotLabelForRevision(snapshots, command.afterRevision).value_or(std::string{})
      });
      if (stewardCommand && !intent.empty()) {
        auto targetName = nodeDisplayName(snapshot, createEffect->targetNodeId);
        if (!targetName) {
          return targetName.error();
        }
        provenance.stewardEdits.push_back(AppStewardEditRow{
          command.id,
          command.afterRevision,
          createEffect->targetNodeId,
          targetName.value(),
          createEffect->payload.displayName,
          intent
        });
      }
    } else if (const auto* updateParam = std::get_if<project::UpdateEffectParamValueCommand>(&parsedCommand.value())) {
      provenance.effects.paramEdits.push_back(EffectParamEditProvenance{
        updateParam->effectNodeId,
        updateParam->paramName,
        command.afterRevision,
        command.sourceKind,
        command.sourceActorName
      });
    } else if (const auto* upsertKeyframe = std::get_if<project::UpsertEffectParamKeyframeCommand>(&parsedCommand.value())) {
      provenance.effects.keyframeEdits.push_back(EffectKeyframeEditProvenance{
        upsertKeyframe->effectNodeId,
        upsertKeyframe->paramName,
        upsertKeyframe->keyframe.id,
        command.afterRevision,
        command.sourceKind,
        command.sourceActorName
      });
    } else if (stewardCommand && !intent.empty()) {
      if (const auto* updateClip = std::get_if<project::UpdateClipCommand>(&parsedCommand.value())) {
        auto targetName = nodeDisplayName(snapshot, updateClip->nodeId);
        if (!targetName) {
          return targetName.error();
        }
        provenance.stewardEdits.push_back(AppStewardEditRow{
          command.id,
          command.afterRevision,
          updateClip->nodeId,
          targetName.value(),
          "Clip Transform",
          intent
        });
      } else if (const auto* addMedia = std::get_if<project::AddMediaToTimelineCommand>(&parsedCommand.value())) {
        auto targetName = nodeDisplayName(snapshot, addMedia->clip.nodeId);
        if (!targetName) {
          return targetName.error();
        }
        provenance.stewardEdits.push_back(AppStewardEditRow{
          command.id,
          command.afterRevision,
          addMedia->clip.nodeId,
          targetName.value(),
          "Timeline Placement",
          intent
        });
      }
    }

    if (command.afterRevision == contentRevision) {
      reachedContentRevision = true;
      break;
    }
  }

  if (!reachedContentRevision) {
    return foundation::Error{
      "app.content_revision_missing",
      "App command provenance requires the current content revision to exist in the command log."
    };
  }
  return provenance;
}

const EffectCreationProvenance* provenanceForEffect(
  const std::vector<EffectCreationProvenance>& provenance,
  const foundation::NodeId& effectNodeId
) {
  const auto match = std::find_if(provenance.rbegin(), provenance.rend(), [&](const EffectCreationProvenance& creation) {
    return creation.effectNodeId == effectNodeId;
  });
  if (match == provenance.rend()) {
    return nullptr;
  }
  return &*match;
}

const EffectParamEditProvenance* latestParamEditForParam(
  const std::vector<EffectParamEditProvenance>& provenance,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName
) {
  const auto match = std::find_if(provenance.rbegin(), provenance.rend(), [&](const EffectParamEditProvenance& edit) {
    return edit.effectNodeId == effectNodeId && edit.paramName == paramName;
  });
  if (match == provenance.rend()) {
    return nullptr;
  }
  return &*match;
}

const EffectKeyframeEditProvenance* latestKeyframeEditForKeyframe(
  const std::vector<EffectKeyframeEditProvenance>& provenance,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  const foundation::KeyframeId& keyframeId
) {
  const auto match = std::find_if(provenance.rbegin(), provenance.rend(), [&](const EffectKeyframeEditProvenance& edit) {
    return edit.effectNodeId == effectNodeId && edit.paramName == paramName && edit.keyframeId == keyframeId;
  });
  if (match == provenance.rend()) {
    return nullptr;
  }
  return &*match;
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

  for (const projection::RenderAudioTrack& track : plan.audioTracks) {
    result.audioTracks.push_back(project::RenderPlanLayerSummary{
      track.sourceNodeId,
      track.name
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

  for (const projection::RenderAudioClip& clip : plan.audioClips) {
    result.audioClips.push_back(project::RenderPlanClipSummary{
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
        return project::ProjectQueryResult{project::GraphResult{
          snapshotResult.value().revision,
          snapshotResult.value().graph
        }};
      } else if constexpr (std::is_same_v<Query, project::GetAssetCatalogQuery>) {
        return project::ProjectQueryResult{project::AssetCatalogResult{
          snapshotResult.value().revision,
          snapshotResult.value().assets
        }};
      } else if constexpr (std::is_same_v<Query, project::InspectCompositionsQuery>) {
        auto result = project::inspectCompositions(snapshotResult.value());
        if (!result) {
          return result.error();
        }
        return project::ProjectQueryResult{result.value()};
      } else if constexpr (std::is_same_v<Query, project::ListNotesQuery>) {
        auto result = project::listNotes(snapshotResult.value());
        if (!result) {
          return result.error();
        }
        return project::ProjectQueryResult{result.value()};
      } else if constexpr (std::is_same_v<Query, project::InspectEffectGraphsQuery>) {
        auto result = project::inspectEffectGraphs(snapshotResult.value());
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
  auto result = buildViewModelAndRenderPlan();
  if (!result) {
    return result.error();
  }
  return std::move(result.value().viewModel);
}

foundation::Result<NativeProjectViewModelResult> NativeProjectSession::buildViewModelAndRenderPlan() const {
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
  auto contentRevision = currentContentRevision(packageState.commandLog.records(), snapshot);
  if (!contentRevision) {
    return contentRevision.error();
  }
  auto commandProvenance = appCommandProvenance(
    packageState.commandLog.records(),
    packageState.snapshots.records(),
    snapshot,
    contentRevision.value()
  );
  if (!commandProvenance) {
    return commandProvenance.error();
  }
  viewModel.steward.edits = std::move(commandProvenance.value().stewardEdits);
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
    } else if (node.kind == graph::NodeKind::Note) {
      const auto* payload = std::get_if<timeline::NotePayload>(&node.payload);
      if (payload != nullptr) {
        viewModel.notes.rows.push_back(AppNoteRow{
          node.id,
          payload->title,
          payload->markdown
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

  for (const projection::RenderAudioTrack& track : plan.audioTracks) {
    viewModel.timeline.audioTracks.push_back(AppLayerRow{
      track.sourceNodeId,
      track.name,
      countClipsForLayer(plan.audioClips, track.sourceNodeId)
    });
  }

  for (const projection::RenderClip& clip : plan.clips) {
    auto assetName = assetNameFor(snapshot.assets, clip.payload.assetId);
    if (!assetName) {
      return assetName.error();
    }
    viewModel.timeline.clips.push_back(AppClipRow{
      clip.sourceNodeId,
      clip.trackNodeId,
      clip.payload.assetId,
      assetName.value(),
      clip.payload.kind,
      clipKindName(clip.payload.kind),
      clip.payload.timelineRange,
      clip.payload.sourceRange,
      clip.payload.playbackRate,
      clip.payload.transform
    });
  }

  for (const projection::RenderAudioClip& clip : plan.audioClips) {
    auto assetName = assetNameFor(snapshot.assets, clip.payload.assetId);
    if (!assetName) {
      return assetName.error();
    }
    viewModel.timeline.audioClips.push_back(AppClipRow{
      clip.sourceNodeId,
      clip.trackNodeId,
      clip.payload.assetId,
      assetName.value(),
      clip.payload.kind,
      clipKindName(clip.payload.kind),
      clip.payload.timelineRange,
      clip.payload.sourceRange,
      clip.payload.playbackRate,
      clip.payload.transform
    });
  }

  for (const projection::RenderCamera& camera : plan.cameras) {
    viewModel.timeline.cameras.push_back(AppCameraRow{
      camera.sourceNodeId,
      camera.name,
      camera.state
    });
  }

  for (const projection::RenderEffectGraph& effectGraph : plan.effectGraphs) {
    auto effectGraphTargetName = nodeDisplayName(snapshot, effectGraph.targetNodeId);
    if (!effectGraphTargetName) {
      return effectGraphTargetName.error();
    }

    AppEffectGraphRow effectGraphRow{
      effectGraph.id,
      effectGraph.targetNodeId,
      effectGraphTargetName.value(),
      effectGraph.nodes.size(),
      effectGraph.edges.size(),
      {}
    };

    for (const projection::RenderEffectNode& effect : effectGraph.nodes) {
      const EffectCreationProvenance* creation = provenanceForEffect(commandProvenance.value().effects.creations, effect.sourceNodeId);
      std::vector<AppEffectParamRow> params;
      params.reserve(effect.payload.params.values.size());
      for (const timeline::Param& param : effect.payload.params.values) {
        const EffectParamEditProvenance* paramEdit = latestParamEditForParam(
          commandProvenance.value().effects.paramEdits,
          effect.sourceNodeId,
          param.name
        );
        std::vector<AppEffectParamRow::Keyframe> keyframes;
        keyframes.reserve(param.keyframes.size());
        for (const timeline::Param::Keyframe& keyframe : param.keyframes) {
          const EffectKeyframeEditProvenance* keyframeEdit = latestKeyframeEditForKeyframe(
            commandProvenance.value().effects.keyframeEdits,
            effect.sourceNodeId,
            param.name,
            keyframe.id
          );
          keyframes.push_back(AppEffectParamRow::Keyframe{
            keyframe.id,
            keyframe.time,
            keyframe.value,
            keyframeEdit == nullptr ? std::nullopt : std::optional<foundation::RevisionId>{keyframeEdit->revision},
            keyframeEdit == nullptr ? std::string{} : keyframeEdit->sourceKind,
            keyframeEdit == nullptr ? std::string{} : keyframeEdit->sourceActorName
          });
        }
        params.push_back(AppEffectParamRow{
          param.name,
          param.control.label,
          param.value,
          paramEdit == nullptr ? std::nullopt : std::optional<foundation::RevisionId>{paramEdit->revision},
          paramEdit == nullptr ? std::string{} : paramEdit->sourceKind,
          paramEdit == nullptr ? std::string{} : paramEdit->sourceActorName,
          param.control.numeric.has_value() ? std::optional<double>{param.control.numeric->min} : std::nullopt,
          param.control.numeric.has_value() ? std::optional<double>{param.control.numeric->max} : std::nullopt,
          param.control.numeric.has_value() ? param.control.numeric->step : std::nullopt,
          std::move(keyframes)
        });
      }

      effectGraphRow.effects.push_back(AppEffectRow{
        effectGraph.id,
        effect.sourceNodeId,
        effectGraph.targetNodeId,
        creation == nullptr ? std::nullopt : std::optional<foundation::RevisionId>{creation->revision},
        creation == nullptr ? std::string{} : creation->sourceKind,
        creation == nullptr ? std::string{} : creation->sourceActorName,
        creation == nullptr ? std::string{} : creation->intent,
        effect.payload.displayName,
        implementationKindName(effect.payload.implementation.kind),
        effect.payload.implementation.entrypoint,
        effect.payload.implementation.kind == timeline::EffectImplementationKind::Builtin &&
          effect.payload.implementation.entrypoint == effects::builtin_effect::CameraTransformEntrypoint,
        effect.payload.activeRange,
        std::move(params)
      });
    }

    viewModel.timeline.effectCount += effectGraphRow.effects.size();
    viewModel.timeline.effectGraphs.push_back(std::move(effectGraphRow));
  }

  return NativeProjectViewModelResult{
    std::move(viewModel),
    std::move(planResult.value().plan)
  };
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

  const storage::ProjectPackageWriter writer;
  std::optional<foundation::FilePath> currentSnapshotPath;
  for (const storage::ProjectPackageSnapshotManifest& snapshotManifest : manifestResult.value().snapshots) {
    const project::ProjectSnapshot* snapshotDocument = session_.findCommittedSnapshot(snapshotManifest.revision);
    if (snapshotDocument == nullptr) {
      return foundation::Error{
        "app.package_snapshot_document_missing",
        "Package save requires a committed snapshot document for revision " + snapshotManifest.revision.value() + "."
      };
    }

    auto snapshotPath = writer.writeSnapshot(storage::ProjectSnapshotWriteRequest{
      state.package,
      *snapshotDocument,
      storage::SnapshotCommitRecord{
        snapshotManifest.id,
        snapshotManifest.documentPath,
        snapshotManifest.label
      }
    });
    if (!snapshotPath) {
      return snapshotPath.error();
    }
    if (snapshotManifest.revision == snapshotResult.value().revision) {
      currentSnapshotPath = snapshotPath.value();
    }
  }
  if (!currentSnapshotPath.has_value()) {
    return foundation::Error{"app.package_snapshot_missing", "Package save requires a snapshot record at the current project head."};
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

  auto schemaMigrationLogPath = writer.writeSchemaMigrationLog(storage::ProjectSchemaMigrationLogWriteRequest{
    state.package,
    manifestResult.value().schemaMigrationLogPath,
    state.schemaMigrationLog
  });
  if (!schemaMigrationLogPath) {
    return schemaMigrationLogPath.error();
  }

  auto manifestPath = writer.writeManifest(manifestResult.value(), state.package);
  if (!manifestPath) {
    return manifestPath.error();
  }

  return NativePackageWriteResult{
    currentSnapshotPath.value(),
    manifestPath.value(),
    commandLogPath.value(),
    eventLogPath.value(),
    schemaMigrationLogPath.value()
  };
}

const project::ProjectSnapshot* NativeProjectSession::findCommittedSnapshot(
  foundation::RevisionId revision
) const noexcept {
  return session_.findCommittedSnapshot(revision);
}

const storage::ProjectPackageState& NativeProjectSession::packageState() const noexcept {
  return session_.packageState();
}

} // namespace grapple::app
