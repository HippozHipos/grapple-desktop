#include <grapple/app/NativeStewardSession.hpp>

#include <grapple/agent/AgentBridge.hpp>
#include <grapple/agent/AgentToolRegistry.hpp>
#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Json.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/runtime/RuntimeOutputNames.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

constexpr const char StewardCreateCameraTransformToolId[] = "steward.create_camera_transform";
constexpr double InitialCameraTransformPositionX = 0.15;
constexpr double InitialCameraTransformPositionY = 0.0;
constexpr double InitialCameraTransformZoom = 1.1;

constexpr const char StewardCreateCameraTransformSchema[] = R"json({
  "type": "object",
  "additionalProperties": false,
  "required": ["cameraNodeId", "intent", "activeRange"],
  "properties": {
    "cameraNodeId": {"type": "string"},
    "intent": {"type": "string"},
    "activeRange": {
      "type": "object",
      "additionalProperties": false,
      "required": ["start", "end"],
      "properties": {
        "start": {"type": "number"},
        "end": {"type": "number"}
      }
    }
  }
})json";

project::CommandSource stewardSource(foundation::RunId runId) {
  return project::CommandSource{
    project::CommandSourceKind::Agent,
    std::move(runId),
    "steward"
  };
}

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

timeline::EffectPayload cameraTransformPayload(foundation::TimeRange activeRange) {
  return timeline::EffectPayload{
    runtime::builtin_effect::CameraTransformDisplayName,
    timeline::EffectImplementation{
      timeline::EffectImplementationKind::Builtin,
      runtime::builtin_effect::CameraTransformEntrypoint,
      timeline::EffectSource{
        timeline::EffectSourceKind::InlineSource,
        "builtin",
        runtime::builtin_effect::CameraTransformSource,
        std::nullopt,
        foundation::stableHash(runtime::builtin_effect::CameraTransformSource)
      }
    },
    timeline::EffectPortSet{
      {timeline::EffectPort{"frame"}},
      {timeline::EffectPort{runtime::output_name::CameraTransform}}
    },
    timeline::ParamSet{
      {
        timeline::Param{
          runtime::builtin_effect::PositionXParam,
          InitialCameraTransformPositionX,
          timeline::Param::Control{
            runtime::builtin_effect::PositionXLabel,
            timeline::Param::NumericControl{-1.0, 1.0, 0.01}
          }
        },
        timeline::Param{
          runtime::builtin_effect::PositionYParam,
          InitialCameraTransformPositionY,
          timeline::Param::Control{
            runtime::builtin_effect::PositionYLabel,
            timeline::Param::NumericControl{-1.0, 1.0, 0.01}
          }
        },
        timeline::Param{
          runtime::builtin_effect::ZoomParam,
          InitialCameraTransformZoom,
          timeline::Param::Control{
            runtime::builtin_effect::ZoomLabel,
            timeline::Param::NumericControl{0.25, 4.0, 0.01}
          }
        }
      }
    },
    activeRange
  };
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

std::string createCameraTransformArgumentsPayload(
  const foundation::NodeId& cameraNodeId,
  const std::string& intent,
  foundation::TimeRange activeRange
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "cameraNodeId", cameraNodeId.value());
  arguments << ',';
  foundation::writeJsonStringProperty(arguments, "intent", intent);
  arguments << ",\"activeRange\":{";
  arguments << "\"start\":" << activeRange.start.value;
  arguments << ",\"end\":" << activeRange.end.value;
  arguments << '}';
  arguments << '}';
  return arguments.str();
}

std::string commandResultJson(const storage::ProjectPackageSessionResult& result) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "commandId", result.commandResult.commandId.value());
  payload << ',';
  foundation::writeJsonStringProperty(payload, "revision", result.snapshot.revision.value());
  payload << '}';
  return payload.str();
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

foundation::Error argumentError(const std::string& path, const std::string& message) {
  return foundation::Error{"steward.tool_argument_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseArgumentsJson(const std::string& json) {
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;

  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return argumentError("$", "Invalid JSON. " + errors);
  }
  if (!root.isObject()) {
    return argumentError("$", "Tool arguments must be a JSON object.");
  }
  return root;
}

foundation::Result<std::string> requiredStringMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  if (!object.isMember(key)) {
    return argumentError(path + "." + key, "Missing required field.");
  }
  if (!object[key].isString()) {
    return argumentError(path + "." + key, "Expected string.");
  }
  return object[key].asString();
}

foundation::Result<double> requiredDoubleMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  if (!object.isMember(key)) {
    return argumentError(path + "." + key, "Missing required field.");
  }
  if (!object[key].isNumeric()) {
    return argumentError(path + "." + key, "Expected number.");
  }
  return object[key].asDouble();
}

foundation::Result<foundation::TimeRange> requiredTimeRangeMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  if (!object.isMember(key)) {
    return argumentError(path + "." + key, "Missing required field.");
  }
  if (!object[key].isObject()) {
    return argumentError(path + "." + key, "Expected object.");
  }
  auto start = requiredDoubleMember(object[key], "start", path + "." + key);
  if (!start) {
    return start.error();
  }
  auto end = requiredDoubleMember(object[key], "end", path + "." + key);
  if (!end) {
    return end.error();
  }
  return foundation::TimeRange{foundation::TimeSeconds{start.value()}, foundation::TimeSeconds{end.value()}};
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

agent::AgentTool makeStewardCreateCameraTransformTool(
  NativeProjectSession& project,
  NativeProjectCommandWriter& commandWriter,
  std::optional<storage::ProjectPackageSessionResult>& packageResult
) {
  return agent::AgentTool{
    foundation::ToolId{StewardCreateCameraTransformToolId},
    StewardCreateCameraTransformToolId,
    "Create Camera Transform",
    "Create an editable Camera Transform effect on a camera.",
    StewardCreateCameraTransformSchema,
    [&](const agent::ToolCall& call, agent::AgentToolContext&) -> foundation::Result<agent::ToolResult> {
      auto arguments = parseArgumentsJson(call.arguments);
      if (!arguments) {
        return arguments.error();
      }
      auto cameraNodeId = requiredStringMember(arguments.value(), "cameraNodeId", "$");
      if (!cameraNodeId) {
        return cameraNodeId.error();
      }
      auto intent = requiredStringMember(arguments.value(), "intent", "$");
      if (!intent) {
        return intent.error();
      }
      auto activeRange = requiredTimeRangeMember(arguments.value(), "activeRange", "$");
      if (!activeRange) {
        return activeRange.error();
      }

      auto snapshot = project.snapshot();
      if (!snapshot) {
        return snapshot.error();
      }
      auto targetReady = ensureCameraCanReceiveTransformEffect(snapshot.value(), foundation::NodeId{cameraNodeId.value()});
      if (!targetReady) {
        return targetReady.error();
      }

      const foundation::SnapshotId snapshotId = commandWriter.nextSnapshotId("steward camera transform");
      auto created = commandWriter.apply(
        project::CreateEffectCommand{
          commandWriter.nextNodeId("effect"),
          foundation::NodeId{cameraNodeId.value()},
          commandWriter.nextEdgeId("effect targets"),
          cameraTransformPayload(activeRange.value()),
          graph::PortName{runtime::output_name::CameraTransform},
          graph::PortName{"input"},
          0
        },
        stewardSource(call.runId),
        storage::SnapshotCommitRecord{
          snapshotId,
          foundation::FilePath{"snapshots/" + snapshotId.value() + ".json"},
          intent.value()
        }
      );
      if (!created) {
        return created.error();
      }

      packageResult = created.value();
      return agent::ToolResult{
        call.toolId,
        agent::ToolResultStatus::Succeeded,
        created.value().snapshot.revision,
        commandResultJson(created.value()),
        {}
      };
    }
  };
}

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
  agent::AgentToolRegistry registry;
  auto registered = registry.registerTool(makeStewardCreateCameraTransformTool(project_, commandWriter_, packageResult));
  if (!registered) {
    return registered.error();
  }

  agent::AgentToolContext toolContext{commandWriter_, project_, commandWriter_};
  agent::AgentBridge bridge{registry, toolContext, events_, nextSequence_};
  auto dispatched = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    runId.value(),
    snapshot.value().info.id,
    snapshot.value().revision,
    toolCallId,
    StewardCreateCameraTransformToolId,
    createCameraTransformArgumentsPayload(cameraNodeId, intent, activeRange)
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
