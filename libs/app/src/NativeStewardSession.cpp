#include <grapple/app/NativeStewardSession.hpp>

#include <grapple/agent/AgentBridge.hpp>
#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Json.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/runtime/RuntimeOutputNames.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

constexpr const char CanonicalCreateEffectToolId[] = "effect.create_node";
constexpr double InitialCameraTransformPositionX = 0.15;
constexpr double InitialCameraTransformPositionY = 0.0;
constexpr double InitialCameraTransformZoom = 1.1;

foundation::Result<void> ensureCameraCanReceiveTransformEffect(
  const project::ProjectSnapshot& snapshot,
  const foundation::NodeId& cameraNodeId
) {
  const graph::GraphNode* selectedNode = snapshot.graph.findNode(cameraNodeId);
  if (selectedNode == nullptr || selectedNode->kind != graph::NodeKind::Camera) {
    return foundation::Error{"steward.selected_node_not_camera", "Camera Transform requires a selected camera node."};
  }

  for (const graph::GraphEdge& edge : snapshot.graph.edges()) {
    if (!edge.enabled ||
        edge.kind != graph::EdgeKind::Targets ||
        edge.targetNodeId != cameraNodeId) {
      continue;
    }

    const graph::GraphNode* effectNode = snapshot.graph.findNode(edge.sourceNodeId);
    if (effectNode == nullptr || effectNode->kind != graph::NodeKind::Effect) {
      return foundation::Error{"steward.effect_node_invalid", "Camera target edge points to a missing or invalid effect node."};
    }

    const auto* effectPayload = std::get_if<timeline::EffectPayload>(&effectNode->payload);
    if (effectPayload == nullptr) {
      return foundation::Error{"steward.effect_payload_invalid", "Camera target effect node must carry an effect payload."};
    }

    if (effectPayload->implementation.kind == timeline::EffectImplementationKind::Builtin &&
        effectPayload->implementation.entrypoint == runtime::builtin_effect::CameraTransformEntrypoint) {
      return foundation::Error{"steward.camera_transform_exists", "Selected camera already has a Camera Transform effect."};
    }
  }

  return {};
}

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

void writeNumericParamJson(
  std::ostream& stream,
  const std::string& name,
  const std::string& label,
  double value,
  double min,
  double max,
  double step
) {
  stream << '{';
  foundation::writeJsonStringProperty(stream, "name", name);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "label", label);
  stream << ",\"value\":" << value;
  stream << ",\"numeric\":{\"min\":" << min
         << ",\"max\":" << max
         << ",\"step\":" << step
         << "}}";
}

std::string createCameraTransformEffectArgumentsPayload(
  const foundation::NodeId& cameraNodeId,
  foundation::TimeRange activeRange
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "targetNodeId", cameraNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "displayName", runtime::builtin_effect::CameraTransformDisplayName);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "implementationKind", "builtin");
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "language", "builtin");
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "entrypoint", runtime::builtin_effect::CameraTransformEntrypoint);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "source", runtime::builtin_effect::CameraTransformSource);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "sourcePort", runtime::output_name::CameraTransform);
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "targetPort", "input");
  arguments << ",\"inputPorts\":[";
  arguments << foundation::jsonQuoted("frame");
  arguments << "],\"outputPorts\":[";
  arguments << foundation::jsonQuoted(runtime::output_name::CameraTransform);
  arguments << ']';
  arguments << ",\"activeRange\":{";
  arguments << "\"start\":" << activeRange.start.value;
  arguments << ",\"end\":" << activeRange.end.value;
  arguments << "},\"params\":[";
  writeNumericParamJson(
    arguments,
    runtime::builtin_effect::PositionXParam,
    runtime::builtin_effect::PositionXLabel,
    InitialCameraTransformPositionX,
    -1.0,
    1.0,
    0.01
  );
  arguments << ',';
  writeNumericParamJson(
    arguments,
    runtime::builtin_effect::PositionYParam,
    runtime::builtin_effect::PositionYLabel,
    InitialCameraTransformPositionY,
    -1.0,
    1.0,
    0.01
  );
  arguments << ',';
  writeNumericParamJson(
    arguments,
    runtime::builtin_effect::ZoomParam,
    runtime::builtin_effect::ZoomLabel,
    InitialCameraTransformZoom,
    0.25,
    4.0,
    0.01
  );
  arguments << ']';
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

class StewardCommandService final : public project::IProjectCommandService {
public:
  StewardCommandService(
    NativeProjectSession& project,
    NativeProjectCommandWriter& commandWriter,
    std::string snapshotLabel,
    std::optional<storage::ProjectPackageSessionResult>& packageResult
  )
    : project_{project},
      commandWriter_{commandWriter},
      snapshotLabel_{std::move(snapshotLabel)},
      packageResult_{packageResult} {}

  foundation::Result<project::ProjectCommandResult> apply(
    const project::ProjectCommandEnvelope& command
  ) override {
    auto snapshot = project_.snapshot();
    if (!snapshot) {
      return snapshot.error();
    }

    const auto* createEffect = std::get_if<project::CreateEffectCommand>(&command.payload);
    if (createEffect == nullptr) {
      return foundation::Error{
        "steward.command_invalid",
        "Steward camera transform can only commit an effect creation command."
      };
    }
    auto targetReady = ensureCameraCanReceiveTransformEffect(snapshot.value(), createEffect->targetNodeId);
    if (!targetReady) {
      return targetReady.error();
    }

    const foundation::SnapshotId snapshotId = commandWriter_.nextSnapshotId("steward camera transform");
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
};

} // namespace

NativeStewardSession::NativeStewardSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter)
  : project_{project},
    commandWriter_{commandWriter} {}

foundation::Result<storage::ProjectPackageSessionResult> NativeStewardSession::createCameraTransformEffect(
  foundation::NodeId cameraNodeId,
  std::string intent,
  foundation::TimeRange activeRange
) {
  auto snapshot = project_.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto runId = startRun(snapshot.value(), "Create editable camera transform");
  if (!runId) {
    return runId.error();
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
  StewardCommandService stewardCommands{project_, commandWriter_, intent, packageResult};
  agent::AgentToolRegistry registry;
  auto registered = registry.registerTool(agent::makeEffectCreateNodeTool());
  if (!registered) {
    return registered.error();
  }

  agent::AgentToolContext toolContext{stewardCommands, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    toolCallId,
    CanonicalCreateEffectToolId,
    createCameraTransformEffectArgumentsPayload(cameraNodeId, activeRange)
  });
  if (!dispatched) {
    markRunStatus(runId.value(), agent::AgentRunStatus::Failed);
    auto diagnostic = appendEvent(runId.value(), agent::AgentRunEventKind::DiagnosticEmitted, diagnosticPayload(dispatched.error()));
    if (!diagnostic) {
      return diagnostic.error();
    }
    auto runFinished = appendEvent(
      runId.value(),
      agent::AgentRunEventKind::RunFinished,
      runFinishedPayload("failed", dispatched.error().message)
    );
    if (!runFinished) {
      return runFinished.error();
    }
    return dispatched.error();
  }
  if (!packageResult.has_value()) {
    return foundation::Error{
      "steward.package_result_missing",
      "Steward camera transform tool succeeded without a committed package result."
    };
  }

  auto runFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::RunFinished,
    runFinishedPayload("succeeded", "Created editable Camera Transform parameters.")
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
