#include <grapple/app/NativeStewardSession.hpp>

#include <grapple/agent/AgentBridge.hpp>
#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/effects/BuiltinEffects.hpp>
#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Json.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace grapple::app {

namespace {

constexpr const char CanonicalEffectCreateNodeToolId[] = "effect.create_node";
constexpr const char CanonicalEffectDeleteNodeToolId[] = "effect.delete_node";
constexpr const char CanonicalEffectCreateParamKeyframeToolId[] = "effect.create_param_keyframe";
constexpr const char CanonicalEffectUpdateParamKeyframeToolId[] = "effect.update_param_keyframe";
constexpr const char CanonicalPlaceAssetToolId[] = "timeline.place_asset";
constexpr const char CanonicalCreateTrackToolId[] = "timeline.create_track";
constexpr const char CanonicalCreateTextClipToolId[] = "timeline.create_text_clip";
constexpr const char CanonicalUpdateTextClipToolId[] = "timeline.update_text_clip";
constexpr const char CanonicalCreateNoteToolId[] = "note.create";
constexpr const char CanonicalUpdateNoteToolId[] = "note.update";
constexpr const char CanonicalDeleteTrackToolId[] = "timeline.delete_track";
constexpr const char CanonicalDeleteClipToolId[] = "timeline.delete_clip";
constexpr const char CanonicalMoveClipToolId[] = "timeline.move_clip";
constexpr const char CanonicalTrimClipToolId[] = "timeline.trim_clip";
constexpr const char CanonicalUpdateClipTransformToolId[] = "timeline.update_clip_transform";
constexpr const char CanonicalUpdateClipPlaybackRateToolId[] = "timeline.update_clip_playback_rate";
constexpr const char CanonicalUpdateEffectParamToolId[] = "effect.update_param_value";

std::string runStartedPayload(const std::string& title) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "title", title);
  payload << '}';
  return payload.str();
}

std::string modelMessagePayload(std::string role, std::string content) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "role", role);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "content", content);
  payload << '}';
  return payload.str();
}

std::string cameraTransformEffectCreateArgumentsPayload(
  const foundation::NodeId& cameraNodeId,
  foundation::TimeRange activeRange,
  CameraTransformIntentDefaults defaults
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "targetNodeId", cameraNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "displayName", effects::builtin_effect::CameraTransformDisplayName);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "implementationKind", "builtin");
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "language", "builtin");
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "entrypoint", effects::builtin_effect::CameraTransformEntrypoint);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "source", effects::builtin_effect::CameraTransformSource);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "sourcePort", effects::output_name::CameraTransform);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "targetPort", "input");
  arguments << ",\"inputPorts\":[\"frame\"]";
  arguments << ",\"outputPorts\":[";
  arguments << foundation::jsonQuoted(effects::output_name::CameraTransform);
  arguments << "]";
  arguments << ",\"activeRange\":{";
  arguments << "\"start\":" << activeRange.start.value;
  arguments << ",\"end\":" << activeRange.end.value;
  arguments << "}";
  arguments << ",\"params\":[";
  arguments << "{\"name\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionXParam);
  arguments << ",\"label\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionXLabel);
  arguments << ",\"value\":" << defaults.positionX;
  arguments << ",\"numeric\":{\"min\":-1,\"max\":1,\"step\":0.01}}";
  arguments << ",{\"name\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionYParam);
  arguments << ",\"label\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::PositionYLabel);
  arguments << ",\"value\":" << defaults.positionY;
  arguments << ",\"numeric\":{\"min\":-1,\"max\":1,\"step\":0.01}}";
  arguments << ",{\"name\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::ZoomParam);
  arguments << ",\"label\":";
  arguments << foundation::jsonQuoted(effects::builtin_effect::ZoomLabel);
  arguments << ",\"value\":" << defaults.zoom;
  arguments << ",\"numeric\":{\"min\":0.25,\"max\":4,\"step\":0.01}}";
  arguments << "]";
  arguments << '}';
  return arguments.str();
}

std::string effectDeleteArgumentsPayload(const foundation::NodeId& effectNodeId) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "effectNodeId", effectNodeId.value());
  arguments << '}';
  return arguments.str();
}

std::string effectCreateParamKeyframeArgumentsPayload(
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  foundation::TimeSeconds time,
  double value
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "effectNodeId", effectNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "paramName", paramName);
  arguments << ",\"time\":" << time.value;
  arguments << ",\"value\":" << value;
  arguments << '}';
  return arguments.str();
}

std::string effectUpdateParamKeyframeArgumentsPayload(
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  const foundation::KeyframeId& keyframeId,
  foundation::TimeSeconds time,
  double value
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "effectNodeId", effectNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "paramName", paramName);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "keyframeId", keyframeId.value());
  arguments << ",\"time\":" << time.value;
  arguments << ",\"value\":" << value;
  arguments << '}';
  return arguments.str();
}

std::string diagnosticPayload(const foundation::Error& error) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "code", error.code);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "severity", "error");
  payload << ',';
  foundation::writeJsonStringProperty(payload, "message", error.message);
  payload << '}';
  return payload.str();
}

std::string runFinishedPayload(const std::string& status, const std::string& summary) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "status", status);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "summary", summary);
  payload << '}';
  return payload.str();
}

std::int64_t stewardRunNumber(const foundation::RunId& runId) {
  constexpr std::string_view prefix{"run_steward_"};
  if (runId.value().rfind(prefix, 0) != 0) {
    return 0;
  }
  const std::string suffix = runId.value().substr(prefix.size());
  if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(), [](unsigned char character) {
    return std::isdigit(character) != 0;
  })) {
    return 0;
  }
  return std::stoll(suffix);
}

foundation::ToolId stewardToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_camera_transform_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardDeleteEffectToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_delete_effect_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardKeyframeToolCallIdForRun(const foundation::RunId& runId, std::int64_t index) {
  return foundation::ToolId{
    "tool_steward_camera_transform_keyframe_" + std::to_string(stewardRunNumber(runId)) + "_" + std::to_string(index)
  };
}

foundation::ToolId stewardPlaceAssetToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_place_asset_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardCreateTextClipToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_create_text_clip_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardCreateTrackToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_create_track_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardUpdateTextClipToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_update_text_clip_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardCreateNoteToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_create_note_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardUpdateNoteToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_update_note_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardDeleteClipToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_delete_clip_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardDeleteTrackToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_delete_track_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardClipMoveToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_clip_move_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardClipTrimToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_clip_trim_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardClipTransformToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_clip_transform_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardClipPlaybackRateToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_clip_playback_rate_" + std::to_string(stewardRunNumber(runId))};
}

foundation::ToolId stewardCameraTransformParamToolCallIdForRun(const foundation::RunId& runId, std::int64_t index) {
  return foundation::ToolId{
    "tool_steward_camera_transform_param_" +
    std::to_string(stewardRunNumber(runId)) +
    "_" +
    std::to_string(index)
  };
}

std::string placeAssetArgumentsPayload(
  const foundation::AssetId& assetId,
  const std::optional<foundation::TimeSeconds>& duration
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "assetId", assetId.value());
  if (duration.has_value()) {
    arguments << ",\"duration\":" << duration->value;
  }
  arguments << '}';
  return arguments.str();
}

void writeTransformArguments(
  std::ostringstream& arguments,
  const timeline::Transform2D& transform
) {
  arguments << "\"position\":{";
  arguments << "\"x\":" << transform.position.x;
  arguments << ",\"y\":" << transform.position.y;
  arguments << "}";
  arguments << ",\"scale\":{";
  arguments << "\"x\":" << transform.scale.x;
  arguments << ",\"y\":" << transform.scale.y;
  arguments << "}";
  arguments << ",\"rotationDegrees\":" << transform.rotationDegrees;
  arguments << ",\"opacity\":" << transform.opacity;
}

void writeTextClipPayloadArgumentsFields(
  std::ostringstream& arguments,
  const timeline::TextClipPayload& payload
) {
  foundation::writeJsonStringProperty(arguments, "text", payload.text);
  arguments << ",\"timelineRange\":{";
  arguments << "\"start\":" << payload.timelineRange.start.value;
  arguments << ",\"end\":" << payload.timelineRange.end.value;
  arguments << "}";
  arguments << ",\"transform\":{";
  writeTransformArguments(arguments, payload.transform);
  arguments << "}";
  arguments << ",\"style\":{";
  arguments << "\"fontSize\":" << payload.style.fontSize;
  arguments << ",\"color\":{";
  arguments << "\"x\":" << payload.style.color.x;
  arguments << ",\"y\":" << payload.style.color.y;
  arguments << ",\"z\":" << payload.style.color.z;
  arguments << "}}";
}

std::string textClipArgumentsPayload(
  const foundation::NodeId& trackNodeId,
  const timeline::TextClipPayload& payload
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "trackNodeId", trackNodeId.value());
  arguments << ',';
  writeTextClipPayloadArgumentsFields(arguments, payload);
  arguments << '}';
  return arguments.str();
}

std::string textClipUpdateArgumentsPayload(
  const foundation::NodeId& clipNodeId,
  const timeline::TextClipPayload& payload
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "clipNodeId", clipNodeId.value());
  arguments << ',';
  writeTextClipPayloadArgumentsFields(arguments, payload);
  arguments << '}';
  return arguments.str();
}

std::string noteCreateArgumentsPayload(const timeline::NotePayload& payload) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "title", payload.title);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "markdown", payload.markdown);
  arguments << '}';
  return arguments.str();
}

std::string noteUpdateArgumentsPayload(
  const foundation::NodeId& noteNodeId,
  const timeline::NotePayload& payload
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "nodeId", noteNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "title", payload.title);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "markdown", payload.markdown);
  arguments << '}';
  return arguments.str();
}

std::string clipTransformArgumentsPayload(
  const foundation::NodeId& clipNodeId,
  const timeline::Transform2D& transform
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "clipNodeId", clipNodeId.value());
  arguments << ',';
  writeTransformArguments(arguments, transform);
  arguments << '}';
  return arguments.str();
}

std::string clipDeleteArgumentsPayload(const foundation::NodeId& clipNodeId) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "clipNodeId", clipNodeId.value());
  arguments << '}';
  return arguments.str();
}

std::string trackKindName(timeline::TrackKind kind) {
  switch (kind) {
    case timeline::TrackKind::Visual:
      return "visual";
    case timeline::TrackKind::Audio:
      return "audio";
  }

  std::abort();
}

std::string trackCreateArgumentsPayload(
  const foundation::NodeId& compositionNodeId,
  const TrackIntentDefaults& defaults
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "compositionNodeId", compositionNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "name", defaults.name);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "kind", trackKindName(defaults.kind));
  arguments << '}';
  return arguments.str();
}

std::string trackDeleteArgumentsPayload(const foundation::NodeId& trackNodeId) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "trackNodeId", trackNodeId.value());
  arguments << '}';
  return arguments.str();
}

std::optional<foundation::NodeId> firstCompositionNodeId(const project::ProjectSnapshot& snapshot) {
  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    if (node.kind == graph::NodeKind::Composition) {
      return node.id;
    }
  }
  return std::nullopt;
}

std::optional<foundation::NodeId> firstVisualTrackNodeId(const project::ProjectSnapshot& snapshot) {
  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    if (node.kind != graph::NodeKind::Track) {
      continue;
    }
    const auto* payload = std::get_if<timeline::TrackPayload>(&node.payload);
    if (payload != nullptr && payload->kind == timeline::TrackKind::Visual) {
      return node.id;
    }
  }
  return std::nullopt;
}

std::string clipMoveArgumentsPayload(
  const foundation::NodeId& clipNodeId,
  foundation::TimeSeconds newStart
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "clipNodeId", clipNodeId.value());
  arguments << ",\"newStart\":" << newStart.value;
  arguments << '}';
  return arguments.str();
}

void writeTimeRangeArguments(
  std::ostringstream& arguments,
  const char* propertyName,
  foundation::TimeRange range
) {
  arguments << ",\"" << propertyName << "\":{"
            << "\"start\":" << range.start.value
            << ",\"end\":" << range.end.value
            << '}';
}

std::string clipTrimArgumentsPayload(
  const foundation::NodeId& clipNodeId,
  foundation::TimeRange timelineRange,
  foundation::TimeRange sourceRange
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "clipNodeId", clipNodeId.value());
  writeTimeRangeArguments(arguments, "timelineRange", timelineRange);
  writeTimeRangeArguments(arguments, "sourceRange", sourceRange);
  arguments << '}';
  return arguments.str();
}

std::string clipPlaybackRateArgumentsPayload(
  const foundation::NodeId& clipNodeId,
  double playbackRate
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "clipNodeId", clipNodeId.value());
  arguments << ",\"playbackRate\":" << playbackRate;
  arguments << '}';
  return arguments.str();
}

std::string effectParamValueArgumentsPayload(
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  double value
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "effectNodeId", effectNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "paramName", paramName);
  arguments << ",\"value\":" << value;
  arguments << '}';
  return arguments.str();
}

foundation::Result<agent::ToolResult> dispatchEffectKeyframeToolCall(
  agent::AgentBridge& bridge,
  const foundation::RunId& runId,
  const foundation::ProjectId& projectId,
  const foundation::RevisionId& expectedRevision,
  const foundation::ToolId& toolCallId,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  const CameraTransformKeyframeAdjustment& keyframe
) {
  if (keyframe.keyframeId.has_value()) {
    return bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
      runId,
      projectId,
      expectedRevision,
      toolCallId,
      CanonicalEffectUpdateParamKeyframeToolId,
      effectUpdateParamKeyframeArgumentsPayload(
        effectNodeId,
        paramName,
        keyframe.keyframeId.value(),
        keyframe.time,
        keyframe.value
      )
    });
  }

  return bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId,
    projectId,
    expectedRevision,
    toolCallId,
    CanonicalEffectCreateParamKeyframeToolId,
    effectCreateParamKeyframeArgumentsPayload(
      effectNodeId,
      paramName,
      keyframe.time,
      keyframe.value
    )
  });
}

class CommittingAgentCommandService final : public project::IProjectCommandService {
public:
  CommittingAgentCommandService(
    NativeProjectSession& project,
    NativeProjectCommandWriter& commandWriter,
    std::string snapshotLabel,
    std::optional<storage::ProjectPackageSessionResult>& packageResult,
    std::optional<foundation::NodeId>* createdNodeId = nullptr
  )
    : project_{project},
      commandWriter_{commandWriter},
      snapshotLabel_{std::move(snapshotLabel)},
      packageResult_{packageResult},
      createdNodeId_{createdNodeId} {}

  foundation::Result<project::ProjectCommandResult> apply(
    const project::ProjectCommandEnvelope& command
  ) override {
    if (createdNodeId_ != nullptr) {
      if (const auto* placement = std::get_if<project::AddMediaToTimelineCommand>(&command.payload)) {
        *createdNodeId_ = placement->clip.nodeId;
      } else if (const auto* track = std::get_if<project::CreateTrackCommand>(&command.payload)) {
        *createdNodeId_ = track->nodeId;
      } else if (const auto* textClip = std::get_if<project::CreateTextClipCommand>(&command.payload)) {
        *createdNodeId_ = textClip->nodeId;
      } else if (const auto* note = std::get_if<project::CreateNoteCommand>(&command.payload)) {
        *createdNodeId_ = note->nodeId;
      }
    }

    const foundation::SnapshotId snapshotId = commandWriter_.nextSnapshotId("steward edit");
    project::ProjectCommandEnvelope stewardCommand = command;
    stewardCommand.source = project::CommandSource{
      project::CommandSourceKind::Agent,
      command.source.runId,
      "steward"
    };

    auto committed = project_.applyAndCommit(
      stewardCommand,
      storage::ProjectCommitRecordOptions{
        std::chrono::system_clock::now(),
        storage::SnapshotCommitRecord{
          snapshotId,
          foundation::FilePath{"snapshots/" + snapshotId.value() + ".json"},
          snapshotLabel_
        }
      }
    );
    if (!committed) {
      return committed.error();
    }

    packageResult_ = committed.value();
    return committed.value().commandResult;
  }

private:
  NativeProjectSession& project_;
  NativeProjectCommandWriter& commandWriter_;
  std::string snapshotLabel_;
  std::optional<storage::ProjectPackageSessionResult>& packageResult_;
  std::optional<foundation::NodeId>* createdNodeId_;
};

} // namespace

NativeStewardSession::NativeStewardSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter)
  : project_{project},
    commandWriter_{commandWriter} {}

foundation::Result<void> NativeStewardSession::finishRunWithError(
  const foundation::RunId& runId,
  const foundation::Error& error
) {
  markRunStatus(runId, agent::AgentRunStatus::Failed);
  auto diagnostic = appendEvent(runId, agent::AgentRunEventKind::DiagnosticEmitted, diagnosticPayload(error));
  if (!diagnostic) {
    return diagnostic.error();
  }
  auto runFinished = appendEvent(
    runId,
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("failed", error.message)
  );
  if (!runFinished) {
    return runFinished.error();
  }
  return {};
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::undoLastEdit(std::string intent) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.undoIntentTargetsLastEdit(intent)) {
    return foundation::Error{
      "steward.undo_intent_mismatch",
      "Steward undo requires an explicit undo or revert-last-edit request."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Undoing the last committed project edit.")
  );
  if (!message) {
    return message.error();
  }

  auto undone = commandWriter_.undoLastCommittedCommand(
    project::CommandSource{
      project::CommandSourceKind::Agent,
      runId.value(),
      "steward"
    },
    std::optional<std::string>{intent}
  );
  if (!undone) {
    auto finished = finishRunWithError(runId.value(), undone.error());
    if (!finished) {
      return finished.error();
    }
    return undone.error();
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Undid the last committed project edit.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return undone.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::redoLastEdit(std::string intent) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.redoIntentTargetsLastUndoneEdit(intent)) {
    return foundation::Error{
      "steward.redo_intent_mismatch",
      "Steward redo requires an explicit redo request."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Redoing the last undone project edit.")
  );
  if (!message) {
    return message.error();
  }

  auto redone = commandWriter_.redoLastUndoneCommand(
    project::CommandSource{
      project::CommandSourceKind::Agent,
      runId.value(),
      "steward"
    },
    std::optional<std::string>{intent}
  );
  if (!redone) {
    auto finished = finishRunWithError(runId.value(), redone.error());
    if (!finished) {
      return finished.error();
    }
    return redone.error();
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Redid the last undone project edit.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return redone.value();
}

foundation::Result<NativeStewardMediaPlacementResult> NativeStewardSession::placeAssetOnTimeline(
  foundation::AssetId assetId,
  std::optional<foundation::TimeSeconds> duration
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  std::string title = "Add selected media to the timeline.";
  const asset::Asset* selectedAsset = snapshot.value().assets.find(assetId);
  if (selectedAsset != nullptr) {
    title = "Add " + selectedAsset->name + " to the timeline.";
  }

  auto runId = startRun(snapshot.value(), title);
  if (!runId) {
    return runId.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Placing the selected media on the timeline as an editable graph change.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  std::optional<foundation::NodeId> placedClipNodeId;
  CommittingAgentCommandService stewardCommands{
    project_,
    commandWriter_,
    title,
    packageResult,
    &placedClipNodeId
  };
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardPlaceAssetToolCallIdForRun(runId.value()),
    CanonicalPlaceAssetToolId,
    placeAssetArgumentsPayload(assetId, duration)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value() || !placedClipNodeId.has_value()) {
    const foundation::Error error{
      "steward.media_placement_result_missing",
      "Steward media placement tool succeeded without a committed placement result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Added selected media to the timeline.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return NativeStewardMediaPlacementResult{packageResult.value(), placedClipNodeId.value()};
}

foundation::Result<NativeStewardTextClipResult> NativeStewardSession::createTextClip(
  std::string intent,
  foundation::TimeSeconds start
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.textClipIntentTargetsText(intent)) {
    return foundation::Error{
      "steward.text_clip_intent_mismatch",
      "Steward text creation requires a text, title, caption, label, or lower third request."
    };
  }

  const std::optional<foundation::NodeId> trackNodeId = firstVisualTrackNodeId(snapshot.value());
  if (!trackNodeId.has_value()) {
    return foundation::Error{
      "steward.visual_track_missing",
      "Steward text creation requires an existing visual track."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  TextClipIntentDefaults defaults = planner_.textClipDefaultsForIntent(intent);
  const timeline::TextClipPayload payload{
    defaults.text,
    foundation::TimeRange{start, foundation::TimeSeconds{start.value + defaults.duration.value}},
    defaults.transform,
    defaults.style
  };

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Creating an editable text clip.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  std::optional<foundation::NodeId> textClipNodeId;
  CommittingAgentCommandService stewardCommands{
    project_,
    commandWriter_,
    intent,
    packageResult,
    &textClipNodeId
  };
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardCreateTextClipToolCallIdForRun(runId.value()),
    CanonicalCreateTextClipToolId,
    textClipArgumentsPayload(trackNodeId.value(), payload)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value() || !textClipNodeId.has_value()) {
    const foundation::Error error{
      "steward.text_clip_result_missing",
      "Steward text creation tool succeeded without a committed text clip result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Created editable text clip.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return NativeStewardTextClipResult{packageResult.value(), textClipNodeId.value()};
}

foundation::Result<NativeStewardNoteResult> NativeStewardSession::createNote(std::string intent) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.noteIntentTargetsNote(intent)) {
    return foundation::Error{
      "steward.note_intent_mismatch",
      "Steward note creation requires a note, rationale, or reminder request."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const NoteIntentDefaults defaults = planner_.noteDefaultsForIntent(intent);
  const timeline::NotePayload payload{defaults.title, defaults.markdown};

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Creating an editable project note.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  std::optional<foundation::NodeId> noteNodeId;
  CommittingAgentCommandService stewardCommands{
    project_,
    commandWriter_,
    intent,
    packageResult,
    &noteNodeId
  };
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardCreateNoteToolCallIdForRun(runId.value()),
    CanonicalCreateNoteToolId,
    noteCreateArgumentsPayload(payload)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value() || !noteNodeId.has_value()) {
    const foundation::Error error{
      "steward.note_result_missing",
      "Steward note creation tool succeeded without a committed note result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Created editable project note.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return NativeStewardNoteResult{packageResult.value(), noteNodeId.value()};
}

foundation::Result<NativeStewardTrackResult> NativeStewardSession::createTrack(std::string intent) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.trackCreateIntentTargetsTrack(intent)) {
    return foundation::Error{
      "steward.track_create_intent_mismatch",
      "Steward track creation requires an explicit add, create, or new track request."
    };
  }

  const std::optional<foundation::NodeId> compositionNodeId = firstCompositionNodeId(snapshot.value());
  if (!compositionNodeId.has_value()) {
    return foundation::Error{
      "steward.composition_missing",
      "Steward track creation requires an existing composition."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const TrackIntentDefaults defaults = planner_.trackDefaultsForIntent(intent);
  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Creating an editable timeline track.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  std::optional<foundation::NodeId> trackNodeId;
  CommittingAgentCommandService stewardCommands{
    project_,
    commandWriter_,
    intent,
    packageResult,
    &trackNodeId
  };
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardCreateTrackToolCallIdForRun(runId.value()),
    CanonicalCreateTrackToolId,
    trackCreateArgumentsPayload(compositionNodeId.value(), defaults)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value() || !trackNodeId.has_value()) {
    const foundation::Error error{
      "steward.track_create_result_missing",
      "Steward track creation tool succeeded without a committed track result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Created editable timeline track.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return NativeStewardTrackResult{packageResult.value(), trackNodeId.value()};
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::editClip(
  foundation::NodeId clipNodeId,
  std::string intent
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const graph::GraphNode* clipNode = snapshot.value().graph.findNode(clipNodeId);
  if (clipNode == nullptr || clipNode->kind != graph::NodeKind::Clip) {
    const foundation::Error error{"steward.clip_missing", "Steward clip edit requires an existing clip node."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  const auto* clipPayload = std::get_if<timeline::ClipPayload>(&clipNode->payload);
  if (clipPayload == nullptr) {
    const foundation::Error error{"steward.clip_payload_missing", "Steward clip edit requires a clip payload."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  auto edit = planner_.clipEditForIntent(*clipPayload, intent);
  if (!edit) {
    auto finished = finishRunWithError(runId.value(), edit.error());
    if (!finished) {
      return finished.error();
    }
    return edit.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Updating the selected clip as an editable graph change.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  foundation::RevisionId expectedRevision = snapshot.value().revision;
  if (edit.value().moveChanged) {
    auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
      runId.value(),
      snapshot.value().info.id,
      expectedRevision,
      stewardClipMoveToolCallIdForRun(runId.value()),
      CanonicalMoveClipToolId,
      clipMoveArgumentsPayload(clipNodeId, edit.value().newStart.value())
    });
    if (!dispatched) {
      auto finished = finishRunWithError(runId.value(), dispatched.error());
      if (!finished) {
        return finished.error();
      }
      return dispatched.error();
    }
    if (packageResult.has_value()) {
      expectedRevision = packageResult->snapshot.revision;
    }
  }
  if (edit.value().trimChanged) {
    auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
      runId.value(),
      snapshot.value().info.id,
      expectedRevision,
      stewardClipTrimToolCallIdForRun(runId.value()),
      CanonicalTrimClipToolId,
      clipTrimArgumentsPayload(
        clipNodeId,
        edit.value().timelineRange.value(),
        edit.value().sourceRange.value()
      )
    });
    if (!dispatched) {
      auto finished = finishRunWithError(runId.value(), dispatched.error());
      if (!finished) {
        return finished.error();
      }
      return dispatched.error();
    }
    if (packageResult.has_value()) {
      expectedRevision = packageResult->snapshot.revision;
    }
  }
  if (edit.value().transformChanged) {
    auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
      runId.value(),
      snapshot.value().info.id,
      expectedRevision,
      stewardClipTransformToolCallIdForRun(runId.value()),
      CanonicalUpdateClipTransformToolId,
      clipTransformArgumentsPayload(clipNodeId, edit.value().transform)
    });
    if (!dispatched) {
      auto finished = finishRunWithError(runId.value(), dispatched.error());
      if (!finished) {
        return finished.error();
      }
      return dispatched.error();
    }
    if (packageResult.has_value()) {
      expectedRevision = packageResult->snapshot.revision;
    }
  }
  if (edit.value().playbackRateChanged) {
    auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
      runId.value(),
      snapshot.value().info.id,
      expectedRevision,
      stewardClipPlaybackRateToolCallIdForRun(runId.value()),
      CanonicalUpdateClipPlaybackRateToolId,
      clipPlaybackRateArgumentsPayload(clipNodeId, edit.value().playbackRate)
    });
    if (!dispatched) {
      auto finished = finishRunWithError(runId.value(), dispatched.error());
      if (!finished) {
        return finished.error();
      }
      return dispatched.error();
    }
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.clip_edit_result_missing",
      "Steward clip edit tool succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Updated selected clip.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::deleteClip(
  foundation::NodeId clipNodeId,
  std::string intent
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.clipDeleteIntentTargetsClip(intent)) {
    return foundation::Error{
      "steward.clip_delete_intent_mismatch",
      "Steward clip deletion requires an explicit delete or remove request for the selected clip."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const graph::GraphNode* clipNode = snapshot.value().graph.findNode(clipNodeId);
  if (clipNode == nullptr || clipNode->kind != graph::NodeKind::Clip) {
    const foundation::Error error{"steward.clip_missing", "Steward clip deletion requires an existing clip node."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Deleting the selected timeline clip.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardDeleteClipToolCallIdForRun(runId.value()),
    CanonicalDeleteClipToolId,
    clipDeleteArgumentsPayload(clipNodeId)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.clip_delete_result_missing",
      "Steward clip delete tool succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Deleted selected timeline clip.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::deleteTrack(
  foundation::NodeId trackNodeId,
  std::string intent
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.trackDeleteIntentTargetsTrack(intent)) {
    return foundation::Error{
      "steward.track_delete_intent_mismatch",
      "Steward track deletion requires an explicit delete or remove request for the selected track."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const graph::GraphNode* trackNode = snapshot.value().graph.findNode(trackNodeId);
  if (trackNode == nullptr || trackNode->kind != graph::NodeKind::Track) {
    const foundation::Error error{"steward.track_missing", "Steward track deletion requires an existing track node."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Deleting the selected timeline track.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardDeleteTrackToolCallIdForRun(runId.value()),
    CanonicalDeleteTrackToolId,
    trackDeleteArgumentsPayload(trackNodeId)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.track_delete_result_missing",
      "Steward track delete tool succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Deleted selected timeline track.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::editTextClip(
  foundation::NodeId clipNodeId,
  std::string intent
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const graph::GraphNode* clipNode = snapshot.value().graph.findNode(clipNodeId);
  if (clipNode == nullptr || clipNode->kind != graph::NodeKind::Clip) {
    const foundation::Error error{"steward.text_clip_missing", "Steward text edit requires an existing text clip node."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  const auto* clipPayload = std::get_if<timeline::TextClipPayload>(&clipNode->payload);
  if (clipPayload == nullptr) {
    const foundation::Error error{"steward.text_clip_payload_missing", "Steward text edit requires a text clip payload."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  auto edit = planner_.textClipEditForIntent(*clipPayload, intent);
  if (!edit) {
    auto finished = finishRunWithError(runId.value(), edit.error());
    if (!finished) {
      return finished.error();
    }
    return edit.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Updating the selected text clip as an editable graph change.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardUpdateTextClipToolCallIdForRun(runId.value()),
    CanonicalUpdateTextClipToolId,
    textClipUpdateArgumentsPayload(clipNodeId, edit.value().payload)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.text_clip_edit_result_missing",
      "Steward text edit tool succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Updated selected text clip.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::editNote(
  foundation::NodeId noteNodeId,
  std::string intent
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const graph::GraphNode* noteNode = snapshot.value().graph.findNode(noteNodeId);
  if (noteNode == nullptr || noteNode->kind != graph::NodeKind::Note) {
    const foundation::Error error{"steward.note_missing", "Steward note edit requires an existing note node."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  const auto* notePayload = std::get_if<timeline::NotePayload>(&noteNode->payload);
  if (notePayload == nullptr) {
    const foundation::Error error{"steward.note_payload_missing", "Steward note edit requires a note payload."};
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }
  auto edit = planner_.noteEditForIntent(*notePayload, intent);
  if (!edit) {
    auto finished = finishRunWithError(runId.value(), edit.error());
    if (!finished) {
      return finished.error();
    }
    return edit.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Updating the selected project note as an editable graph change.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardUpdateNoteToolCallIdForRun(runId.value()),
    CanonicalUpdateNoteToolId,
    noteUpdateArgumentsPayload(noteNodeId, edit.value().payload)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.note_edit_result_missing",
      "Steward note edit tool succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Updated selected project note.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

bool NativeStewardSession::clipEditIntentTargetsClip(const std::string& intent) const {
  return planner_.clipEditIntentTargetsClip(intent);
}

bool NativeStewardSession::clipDeleteIntentTargetsClip(const std::string& intent) const {
  return planner_.clipDeleteIntentTargetsClip(intent);
}

bool NativeStewardSession::trackCreateIntentTargetsTrack(const std::string& intent) const {
  return planner_.trackCreateIntentTargetsTrack(intent);
}

bool NativeStewardSession::trackDeleteIntentTargetsTrack(const std::string& intent) const {
  return planner_.trackDeleteIntentTargetsTrack(intent);
}

bool NativeStewardSession::textClipIntentTargetsText(const std::string& intent) const {
  return planner_.textClipIntentTargetsText(intent);
}

bool NativeStewardSession::textClipEditIntentTargetsTextClip(const std::string& intent) const {
  return planner_.textClipEditIntentTargetsTextClip(intent);
}

bool NativeStewardSession::noteIntentTargetsNote(const std::string& intent) const {
  return planner_.noteIntentTargetsNote(intent);
}

bool NativeStewardSession::noteEditIntentTargetsNote(const std::string& intent) const {
  return planner_.noteEditIntentTargetsNote(intent);
}

bool NativeStewardSession::undoIntentTargetsLastEdit(const std::string& intent) const {
  return planner_.undoIntentTargetsLastEdit(intent);
}

bool NativeStewardSession::redoIntentTargetsLastUndoneEdit(const std::string& intent) const {
  return planner_.redoIntentTargetsLastUndoneEdit(intent);
}

bool NativeStewardSession::historyIntentTargetsEdit(const std::string& intent) const {
  return planner_.historyIntentTargetsEdit(intent);
}

bool NativeStewardSession::cameraTransformDeleteIntentTargetsCameraControls(const std::string& intent) const {
  return planner_.cameraTransformDeleteIntentTargetsCameraControls(intent);
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::adjustCameraTransformControls(
  foundation::NodeId cameraNodeId,
  std::string intent,
  foundation::TimeRange activeRange
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  const bool motionRequested = planner_.cameraIntentRequestsExplicitMotion(intent);
  std::optional<CameraTransformMotionKeyframes> motion;
  std::vector<CameraTransformParamAdjustment> adjustments;
  std::vector<std::vector<CameraTransformKeyframeAdjustment>> keyframeAdjustmentsByParam;
  if (motionRequested) {
    auto motionAdjustment = planner_.cameraTransformMotionAdjustmentForIntent(snapshot.value(), cameraNodeId, intent, activeRange);
    if (!motionAdjustment) {
      auto finished = finishRunWithError(runId.value(), motionAdjustment.error());
      if (!finished) {
        return finished.error();
      }
      return motionAdjustment.error();
    }
    motion = motionAdjustment.value();
  } else {
    auto paramAdjustments = planner_.cameraTransformParamAdjustmentsForIntent(snapshot.value(), cameraNodeId, intent);
    if (!paramAdjustments) {
      auto finished = finishRunWithError(runId.value(), paramAdjustments.error());
      if (!finished) {
        return finished.error();
      }
      return paramAdjustments.error();
    }
    if (paramAdjustments.value().empty()) {
      const foundation::Error error{
        "steward.camera_transform_noop",
        "Camera Transform controls already match the requested adjustment."
      };
      auto finished = finishRunWithError(runId.value(), error);
      if (!finished) {
        return finished.error();
      }
      return error;
    }
    adjustments = std::move(paramAdjustments.value());
    keyframeAdjustmentsByParam.reserve(adjustments.size());
    for (const CameraTransformParamAdjustment& paramAdjustment : adjustments) {
      auto adjustedKeyframes = planner_.adjustedCameraTransformKeyframes(snapshot.value(), paramAdjustment);
      if (!adjustedKeyframes) {
        auto finished = finishRunWithError(runId.value(), adjustedKeyframes.error());
        if (!finished) {
          return finished.error();
        }
        return adjustedKeyframes.error();
      }
      keyframeAdjustmentsByParam.push_back(std::move(adjustedKeyframes.value()));
    }
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload(
      "assistant",
      motion.has_value()
        ? "Updating the existing Camera Transform controls as editable motion keyframes."
        : std::any_of(keyframeAdjustmentsByParam.begin(), keyframeAdjustmentsByParam.end(), [](const auto& keyframes) { return !keyframes.empty(); })
          ? "Updating the existing Camera Transform keyframes as an editable parameter change."
          : "Updating the existing Camera Transform controls as an editable parameter change."
    )
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  foundation::RevisionId latestRevision = snapshot.value().revision;
  if (motion.has_value()) {
    foundation::NodeId effectNodeId;
    const timeline::EffectPayload* payload = planner_.cameraTransformEffectPayload(snapshot.value(), cameraNodeId, effectNodeId);
    if (payload == nullptr) {
      const foundation::Error error{
        "steward.camera_transform_missing",
        "Steward camera control adjustment requires existing Camera Transform controls."
      };
      auto finished = finishRunWithError(runId.value(), error);
      if (!finished) {
        return finished.error();
      }
      return error;
    }
    auto startKeyframeId = planner_.effectParamKeyframeIdAtTime(*payload, motion->paramName, activeRange.start);
    if (!startKeyframeId) {
      auto finished = finishRunWithError(runId.value(), startKeyframeId.error());
      if (!finished) {
        return finished.error();
      }
      return startKeyframeId.error();
    }
    auto endKeyframeId = planner_.effectParamKeyframeIdAtTime(*payload, motion->paramName, motion->endTime);
    if (!endKeyframeId) {
      auto finished = finishRunWithError(runId.value(), endKeyframeId.error());
      if (!finished) {
        return finished.error();
      }
      return endKeyframeId.error();
    }

    auto startKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 1),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        startKeyframeId.value(),
        activeRange.start,
        motion->startValue
      }
    );
    if (!startKeyframe) {
      auto finished = finishRunWithError(runId.value(), startKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return startKeyframe.error();
    }
    latestRevision = startKeyframe.value().observedRevision;

    auto endKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 2),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        endKeyframeId.value(),
        motion->endTime,
        motion->endValue
      }
    );
    if (!endKeyframe) {
      auto finished = finishRunWithError(runId.value(), endKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return endKeyframe.error();
    }
  } else {
    std::int64_t toolCallIndex = 1;
    for (std::size_t adjustmentIndex = 0; adjustmentIndex < adjustments.size(); ++adjustmentIndex) {
      const CameraTransformParamAdjustment& paramAdjustment = adjustments[adjustmentIndex];
      const std::vector<CameraTransformKeyframeAdjustment>& keyframeAdjustments = keyframeAdjustmentsByParam[adjustmentIndex];
      if (!keyframeAdjustments.empty()) {
        for (const CameraTransformKeyframeAdjustment& keyframe : keyframeAdjustments) {
          auto keyframeResult = dispatchEffectKeyframeToolCall(
            bridge,
            runId.value(),
            snapshot.value().info.id,
            latestRevision,
            stewardKeyframeToolCallIdForRun(runId.value(), toolCallIndex),
            paramAdjustment.effectNodeId,
            paramAdjustment.paramName,
            keyframe
          );
          if (!keyframeResult) {
            auto finished = finishRunWithError(runId.value(), keyframeResult.error());
            if (!finished) {
              return finished.error();
            }
            return keyframeResult.error();
          }
          latestRevision = keyframeResult.value().observedRevision;
          ++toolCallIndex;
        }
      } else {
        auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
          runId.value(),
          snapshot.value().info.id,
          latestRevision,
          stewardCameraTransformParamToolCallIdForRun(runId.value(), toolCallIndex),
          CanonicalUpdateEffectParamToolId,
          effectParamValueArgumentsPayload(
            paramAdjustment.effectNodeId,
            paramAdjustment.paramName,
            paramAdjustment.value
          )
        });
        if (!dispatched) {
          auto finished = finishRunWithError(runId.value(), dispatched.error());
          if (!finished) {
            return finished.error();
          }
          return dispatched.error();
        }
        latestRevision = dispatched.value().observedRevision;
        ++toolCallIndex;
      }
    }
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.camera_transform_update_result_missing",
      "Steward camera control update succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload(
      "succeeded",
      motion.has_value()
        ? "Updated existing Camera Transform motion keyframes."
        : std::any_of(keyframeAdjustmentsByParam.begin(), keyframeAdjustmentsByParam.end(), [](const auto& keyframes) { return !keyframes.empty(); })
          ? "Updated existing Camera Transform keyframes."
          : "Updated existing Camera Transform controls."
    )
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::createCameraTransformEffect(
  foundation::NodeId cameraNodeId,
  std::string intent,
  foundation::TimeRange activeRange
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  foundation::NodeId existingEffectNodeId;
  if (planner_.cameraTransformEffectPayload(snapshot.value(), cameraNodeId, existingEffectNodeId) != nullptr) {
    const foundation::Error error{
      "agent.camera_transform_exists",
      "Camera already has Camera Transform controls."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Creating an editable Camera Transform effect for the selected camera.")
  );
  if (!message) {
    return message.error();
  }

  const foundation::ToolId toolCallId = stewardToolCallIdForRun(runId.value());
  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  const CameraTransformIntentDefaults defaults = planner_.cameraTransformDefaultsForIntent(intent);
  const std::optional<CameraTransformMotionKeyframes> motion = planner_.cameraMotionKeyframesForIntent(intent, activeRange);
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    toolCallId,
    CanonicalEffectCreateNodeToolId,
    cameraTransformEffectCreateArgumentsPayload(cameraNodeId, activeRange, defaults)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  foundation::RevisionId latestRevision = dispatched.value().observedRevision;

  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.package_result_missing",
      "Steward camera transform effect creation succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  foundation::NodeId effectNodeId;
  const timeline::EffectPayload* createdPayload = planner_.cameraTransformEffectPayload(
    packageResult->snapshot,
    cameraNodeId,
    effectNodeId
  );
  if (createdPayload == nullptr) {
    const foundation::Error error{
      "steward.camera_transform_effect_missing",
      "Created Camera Transform effect was not found on the target camera."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  if (motion.has_value()) {
    auto startKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 1),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        std::nullopt,
        activeRange.start,
        motion->startValue
      }
    );
    if (!startKeyframe) {
      auto finished = finishRunWithError(runId.value(), startKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return startKeyframe.error();
    }
    latestRevision = startKeyframe.value().observedRevision;

    auto endKeyframe = dispatchEffectKeyframeToolCall(
      bridge,
      runId.value(),
      snapshot.value().info.id,
      latestRevision,
      stewardKeyframeToolCallIdForRun(runId.value(), 2),
      effectNodeId,
      motion->paramName,
      CameraTransformKeyframeAdjustment{
        std::nullopt,
        motion->endTime,
        motion->endValue
      }
    );
    if (!endKeyframe) {
      auto finished = finishRunWithError(runId.value(), endKeyframe.error());
      if (!finished) {
        return finished.error();
      }
      return endKeyframe.error();
    }
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload(
      "succeeded",
      motion.has_value()
        ? "Created editable Camera Transform parameters and motion keyframes."
        : "Created editable Camera Transform parameters."
    )
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::deleteCameraTransformEffect(
  foundation::NodeId cameraNodeId,
  std::string intent
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  if (!planner_.cameraTransformDeleteIntentTargetsCameraControls(intent)) {
    return foundation::Error{
      "steward.camera_transform_delete_intent_mismatch",
      "Deleting Camera Transform controls requires an explicit delete or remove request for camera controls."
    };
  }

  foundation::NodeId effectNodeId;
  if (planner_.cameraTransformEffectPayload(snapshot.value(), cameraNodeId, effectNodeId) == nullptr) {
    return foundation::Error{
      "steward.camera_transform_missing",
      "Deleting Camera Transform controls requires existing Camera Transform controls."
    };
  }

  auto runId = startRun(snapshot.value(), intent);
  if (!runId) {
    return runId.error();
  }

  auto message = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ModelMessage,
    modelMessagePayload("assistant", "Deleting the Camera Transform controls.")
  );
  if (!message) {
    return message.error();
  }

  std::optional<storage::ProjectPackageSessionResult> packageResult;
  CommittingAgentCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = agent::registerProjectTools(registry);
  if (!registered) {
    auto finished = finishRunWithError(runId.value(), registered.error());
    if (!finished) {
      return finished.error();
    }
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    stewardDeleteEffectToolCallIdForRun(runId.value()),
    CanonicalEffectDeleteNodeToolId,
    effectDeleteArgumentsPayload(effectNodeId)
  });
  if (!dispatched) {
    auto finished = finishRunWithError(runId.value(), dispatched.error());
    if (!finished) {
      return finished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value()) {
    const foundation::Error error{
      "steward.camera_transform_delete_result_missing",
      "Steward Camera Transform delete tool succeeded without a committed package result."
    };
    auto finished = finishRunWithError(runId.value(), error);
    if (!finished) {
      return finished.error();
    }
    return error;
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Deleted Camera Transform controls.")
  );
  if (!runFinished) {
    return runFinished.error();
  }

  markRunStatus(runId.value(), agent::AgentRunStatus::Succeeded);
  return packageResult.value();
}

agent::AgentConversationState NativeStewardSession::conversationState() const {
  const agent::AgentConversationStateProjector projector;
  return projector.project(runs_, events_.records());
}

const std::vector<agent::AgentRun>& NativeStewardSession::runs() const noexcept {
  return runs_;
}

const std::vector<agent::AgentRunEvent>& NativeStewardSession::events() const noexcept {
  return events_.records();
}

foundation::Result<void> NativeStewardSession::restoreConversation(
  std::vector<agent::AgentRun> runs,
  std::vector<agent::AgentRunEvent> events
) {
  agent::AgentRunEventLog restoredEvents;
  for (const agent::AgentRunEvent& event : events) {
    auto appended = restoredEvents.append(event);
    if (!appended) {
      return appended.error();
    }
  }

  std::int64_t nextRunNumber = 1;
  for (const agent::AgentRun& run : runs) {
    nextRunNumber = std::max(nextRunNumber, stewardRunNumber(run.id) + 1);
  }

  std::int64_t nextSequence = 1;
  for (const agent::AgentRunEvent& event : restoredEvents.records()) {
    nextSequence = std::max(nextSequence, event.sequence + 1);
  }

  runs_ = std::move(runs);
  events_ = std::move(restoredEvents);
  nextRunNumber_ = nextRunNumber;
  nextSequence_ = nextSequence;
  return {};
}

foundation::Result<foundation::RunId> NativeStewardSession::startRun(
  const project::ProjectSnapshot& snapshot,
  const std::string& title
) {
  const foundation::RunId runId{"run_steward_" + std::to_string(nextRunNumber_++)};
  auto started = appendEvent(runId, agent::AgentRunEventKind::RunStarted, runStartedPayload(title));
  if (!started) {
    return started.error();
  }
  runs_.push_back(agent::AgentRun{
    runId,
    snapshot.info.id,
    std::nullopt,
    agent::AgentRunStatus::Running,
    std::chrono::system_clock::now()
  });
  return runId;
}

foundation::Result<void> NativeStewardSession::appendEvent(
  foundation::RunId runId,
  agent::AgentRunEventKind kind,
  std::string payloadJson
) {
  auto appended = events_.append(agent::AgentRunEvent{
    std::move(runId),
    nextSequence_,
    kind,
    std::move(payloadJson),
    std::chrono::system_clock::now()
  });
  if (!appended) {
    return appended.error();
  }
  ++nextSequence_;
  return {};
}

void NativeStewardSession::markRunStatus(const foundation::RunId& runId, agent::AgentRunStatus status) {
  for (agent::AgentRun& run : runs_) {
    if (run.id == runId) {
      run.status = status;
      return;
    }
  }
}

} // namespace grapple::app
