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
constexpr const char CanonicalEffectCreateParamKeyframeToolId[] = "effect.create_param_keyframe";
constexpr const char CanonicalEffectUpdateParamKeyframeToolId[] = "effect.update_param_keyframe";
constexpr const char CanonicalPlaceAssetToolId[] = "timeline.place_asset";
constexpr const char CanonicalCreateTextClipToolId[] = "timeline.create_text_clip";
constexpr const char CanonicalUpdateTextClipToolId[] = "timeline.update_text_clip";
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

foundation::ToolId stewardUpdateTextClipToolCallIdForRun(const foundation::RunId& runId) {
  return foundation::ToolId{"tool_steward_update_text_clip_" + std::to_string(stewardRunNumber(runId))};
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
    std::optional<foundation::NodeId>* createdClipNodeId = nullptr
  )
    : project_{project},
      commandWriter_{commandWriter},
      snapshotLabel_{std::move(snapshotLabel)},
      packageResult_{packageResult},
      createdClipNodeId_{createdClipNodeId} {}

  foundation::Result<project::ProjectCommandResult> apply(
    const project::ProjectCommandEnvelope& command
  ) override {
    if (createdClipNodeId_ != nullptr) {
      if (const auto* placement = std::get_if<project::AddMediaToTimelineCommand>(&command.payload)) {
        *createdClipNodeId_ = placement->clip.nodeId;
      } else if (const auto* textClip = std::get_if<project::CreateTextClipCommand>(&command.payload)) {
        *createdClipNodeId_ = textClip->nodeId;
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
  std::optional<foundation::NodeId>* createdClipNodeId_;
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

bool NativeStewardSession::clipEditIntentTargetsClip(const std::string& intent) const {
  return planner_.clipEditIntentTargetsClip(intent);
}

bool NativeStewardSession::textClipIntentTargetsText(const std::string& intent) const {
  return planner_.textClipIntentTargetsText(intent);
}

bool NativeStewardSession::textClipEditIntentTargetsTextClip(const std::string& intent) const {
  return planner_.textClipEditIntentTargetsTextClip(intent);
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
