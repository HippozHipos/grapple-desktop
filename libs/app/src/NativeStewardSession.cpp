#include <grapple/app/NativeStewardSession.hpp>

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Json.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/runtime/BuiltinEffects.hpp>
#include <grapple/runtime/RuntimeOutputNames.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

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
          0.0,
          timeline::Param::Control{
            runtime::builtin_effect::PositionXLabel,
            timeline::Param::NumericControl{-1.0, 1.0, 0.01}
          }
        },
        timeline::Param{
          runtime::builtin_effect::PositionYParam,
          0.0,
          timeline::Param::Control{
            runtime::builtin_effect::PositionYLabel,
            timeline::Param::NumericControl{-1.0, 1.0, 0.01}
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
  foundation::TimeRange activeRange
) {
  std::ostringstream arguments;
  arguments << '{';
  foundation::writeJsonStringProperty(arguments, "cameraNodeId", cameraNodeId.value());
  arguments << ",\"activeRangeStart\":" << activeRange.start.value;
  arguments << ",\"activeRangeEnd\":" << activeRange.end.value;
  arguments << '}';
  return arguments.str();
}

std::string toolCallStartedPayload(
  const foundation::ToolId& toolCallId,
  const std::string& toolSerializedId,
  const std::string& argumentsJson
) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "toolCallId", toolCallId.value());
  payload << ',';
  foundation::writeJsonStringProperty(payload, "toolSerializedId", toolSerializedId);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "argumentsJson", argumentsJson);
  payload << '}';
  return payload.str();
}

std::string toolCallFinishedPayload(
  const foundation::ToolId& toolCallId,
  const std::string& status,
  const std::string& resultJson
) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "toolCallId", toolCallId.value());
  payload << ',';
  foundation::writeJsonStringProperty(payload, "status", status);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "resultJson", resultJson);
  payload << '}';
  return payload.str();
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

std::string errorResultJson(const foundation::Error& error) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "code", error.code);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "message", error.message);
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

std::string runFinishedPayload(const std::string& status, const std::string& summary) {
  std::ostringstream payload;
  payload << '{';
  foundation::writeJsonStringProperty(payload, "status", status);
  payload << ',';
  foundation::writeJsonStringProperty(payload, "summary", summary);
  payload << '}';
  return payload.str();
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

  const foundation::ToolId toolCallId{"tool_steward_camera_transform_" + std::to_string(nextToolNumber_++)};
  constexpr const char* toolSerializedId = "steward.create_camera_transform";
  auto toolStarted = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ToolCallStarted,
    toolCallStartedPayload(
      toolCallId,
      toolSerializedId,
      createCameraTransformArgumentsPayload(cameraNodeId, activeRange)
    )
  );
  if (!toolStarted) {
    return toolStarted.error();
  }

  auto targetReady = ensureCameraCanReceiveTransformEffect(snapshot.value(), cameraNodeId);
  if (!targetReady) {
    markRunStatus(runId.value(), agent::AgentRunStatus::Failed);
    auto diagnostic = appendEvent(runId.value(), agent::AgentRunEventKind::DiagnosticEmitted, diagnosticPayload(targetReady.error()));
    if (!diagnostic) {
      return diagnostic.error();
    }
    auto toolFinished = appendEvent(
      runId.value(),
      agent::AgentRunEventKind::ToolCallFinished,
      toolCallFinishedPayload(toolCallId, "failed", errorResultJson(targetReady.error()))
    );
    if (!toolFinished) {
      return toolFinished.error();
    }
    auto runFinished = appendEvent(
      runId.value(),
      agent::AgentRunEventKind::RunFinished,
      runFinishedPayload("failed", targetReady.error().message)
    );
    if (!runFinished) {
      return runFinished.error();
    }
    return targetReady.error();
  }

  const foundation::SnapshotId snapshotId = commandWriter_.nextSnapshotId("steward camera transform");
  auto created = commandWriter_.apply(
    project::CreateEffectCommand{
      commandWriter_.nextNodeId("effect"),
      cameraNodeId,
      commandWriter_.nextEdgeId("effect targets"),
      cameraTransformPayload(activeRange),
      graph::PortName{runtime::output_name::CameraTransform},
      graph::PortName{"input"},
      0
    },
    stewardSource(runId.value()),
    storage::SnapshotCommitRecord{
      snapshotId,
      foundation::FilePath{"snapshots/" + snapshotId.value() + ".json"},
      std::move(intent)
    }
  );
  if (!created) {
    markRunStatus(runId.value(), agent::AgentRunStatus::Failed);
    auto diagnostic = appendEvent(runId.value(), agent::AgentRunEventKind::DiagnosticEmitted, diagnosticPayload(created.error()));
    if (!diagnostic) {
      return diagnostic.error();
    }
    auto toolFinished = appendEvent(
      runId.value(),
      agent::AgentRunEventKind::ToolCallFinished,
      toolCallFinishedPayload(toolCallId, "failed", errorResultJson(created.error()))
    );
    if (!toolFinished) {
      return toolFinished.error();
    }
    auto runFinished = appendEvent(
      runId.value(),
      agent::AgentRunEventKind::RunFinished,
      runFinishedPayload("failed", created.error().message)
    );
    if (!runFinished) {
      return runFinished.error();
    }
    return created.error();
  }

  auto toolFinished = appendEvent(
    runId.value(),
    agent::AgentRunEventKind::ToolCallFinished,
    toolCallFinishedPayload(toolCallId, "succeeded", commandResultJson(created.value()))
  );
  if (!toolFinished) {
    return toolFinished.error();
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
  return created.value();
}

agent::AgentConversationState NativeStewardSession::conversationState() const {
  const agent::AgentConversationStateProjector projector;
  return projector.project(runs_, events_.records());
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
