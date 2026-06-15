#include <grapple/agent/AgentConversationState.hpp>

#include <json/json.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace grapple::agent {

namespace {

std::vector<AgentRunEvent> orderedEvents(std::span<const AgentRunEvent> events) {
  std::vector<AgentRunEvent> ordered{events.begin(), events.end()};
  std::stable_sort(ordered.begin(), ordered.end(), [](const AgentRunEvent& left, const AgentRunEvent& right) {
    return left.sequence < right.sequence;
  });
  return ordered;
}

const AgentRun* findRunMetadata(std::span<const AgentRun> runs, const foundation::RunId& runId) {
  const auto run = std::find_if(runs.begin(), runs.end(), [&](const AgentRun& candidate) {
    return candidate.id == runId;
  });
  if (run == runs.end()) {
    return nullptr;
  }
  return &*run;
}

AgentConversationRunState* findRunState(std::vector<AgentConversationRunState>& runs, const foundation::RunId& runId) {
  const auto run = std::find_if(runs.begin(), runs.end(), [&](const AgentConversationRunState& candidate) {
    return candidate.runId == runId;
  });
  if (run == runs.end()) {
    return nullptr;
  }
  return &*run;
}

AgentConversationToolCall* findToolCall(
  std::vector<AgentConversationToolCall>& toolCalls,
  const foundation::ToolId& toolCallId
) {
  const auto toolCall = std::find_if(toolCalls.begin(), toolCalls.end(), [&](const AgentConversationToolCall& candidate) {
    return candidate.toolCallId == toolCallId;
  });
  if (toolCall == toolCalls.end()) {
    return nullptr;
  }
  return &*toolCall;
}

AgentDelegatedRunState* findDelegatedRun(
  std::vector<AgentDelegatedRunState>& delegatedRuns,
  const foundation::RunId& runId
) {
  const auto delegatedRun = std::find_if(delegatedRuns.begin(), delegatedRuns.end(), [&](const AgentDelegatedRunState& candidate) {
    return candidate.runId == runId;
  });
  if (delegatedRun == delegatedRuns.end()) {
    return nullptr;
  }
  return &*delegatedRun;
}

void addProjectionDiagnostic(
  AgentConversationState& state,
  const AgentRunEvent& event,
  std::string code,
  std::string message
) {
  state.diagnostics.push_back(AgentConversationProjectionDiagnostic{
    event.runId,
    event.sequence,
    std::move(code),
    std::move(message)
  });
}

std::optional<Json::Value> parsePayload(AgentConversationState& state, const AgentRunEvent& event) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(event.payloadJson.data(), event.payloadJson.data() + event.payloadJson.size(), &root, &errors) ||
      !root.isObject()) {
    addProjectionDiagnostic(
      state,
      event,
      "agent.event_payload_invalid",
      std::string{"Invalid payload for "} + serializedAgentRunEventKind(event.kind) + "."
    );
    return std::nullopt;
  }
  return root;
}

std::optional<std::string> optionalString(const Json::Value& object, const char* key) {
  if (!object.isMember(key)) {
    return std::nullopt;
  }
  if (!object[key].isString()) {
    return std::nullopt;
  }
  return object[key].asString();
}

std::optional<std::string> requiredString(
  AgentConversationState& state,
  const AgentRunEvent& event,
  const Json::Value& object,
  const char* key
) {
  auto value = optionalString(object, key);
  if (!value.has_value()) {
    addProjectionDiagnostic(
      state,
      event,
      "agent.event_payload_field_missing",
      std::string{"Missing string field "} + key + " for " + serializedAgentRunEventKind(event.kind) + "."
    );
  }
  return value;
}

AgentRunStatus parseRunStatus(const std::string& status) {
  if (status == "pending") {
    return AgentRunStatus::Pending;
  }
  if (status == "running") {
    return AgentRunStatus::Running;
  }
  if (status == "succeeded") {
    return AgentRunStatus::Succeeded;
  }
  if (status == "failed") {
    return AgentRunStatus::Failed;
  }
  return AgentRunStatus::Failed;
}

AgentConversationToolCallStatus parseToolCallStatus(const std::string& status) {
  if (status == "succeeded") {
    return AgentConversationToolCallStatus::Succeeded;
  }
  if (status == "failed") {
    return AgentConversationToolCallStatus::Failed;
  }
  return AgentConversationToolCallStatus::Running;
}

DiagnosticSeverity parseDiagnosticSeverity(const std::string& severity) {
  if (severity == "info") {
    return DiagnosticSeverity::Info;
  }
  if (severity == "warning") {
    return DiagnosticSeverity::Warning;
  }
  return DiagnosticSeverity::Error;
}

void applyRunStarted(
  AgentConversationState& state,
  std::span<const AgentRun> runs,
  const AgentRunEvent& event,
  const Json::Value& payload
) {
  if (findRunState(state.runs, event.runId) != nullptr) {
    addProjectionDiagnostic(state, event, "agent.run_started_duplicate", "Run was started more than once.");
    return;
  }

  const AgentRun* metadata = findRunMetadata(runs, event.runId);
  if (metadata == nullptr) {
    addProjectionDiagnostic(state, event, "agent.run_metadata_missing", "Run start event has no run metadata.");
  }

  state.runs.push_back(AgentConversationRunState{
    event.runId,
    metadata != nullptr ? metadata->projectId : foundation::ProjectId{},
    metadata != nullptr ? metadata->parentRunId : std::nullopt,
    AgentRunStatus::Running,
    optionalString(payload, "title").value_or(""),
    "",
    {},
    {},
    {},
    {}
  });
}

void applyModelMessage(
  AgentConversationState& state,
  AgentConversationRunState& run,
  const AgentRunEvent& event,
  const Json::Value& payload
) {
  auto role = requiredString(state, event, payload, "role");
  auto content = requiredString(state, event, payload, "content");
  if (!role.has_value() || !content.has_value()) {
    return;
  }

  run.messages.push_back(AgentConversationMessage{
    event.runId,
    event.sequence,
    role.value(),
    content.value()
  });
}

void applyToolCallStarted(
  AgentConversationState& state,
  AgentConversationRunState& run,
  const AgentRunEvent& event,
  const Json::Value& payload
) {
  auto toolCallId = requiredString(state, event, payload, "toolCallId");
  auto toolSerializedId = requiredString(state, event, payload, "toolSerializedId");
  if (!toolCallId.has_value() || !toolSerializedId.has_value()) {
    return;
  }

  run.toolCalls.push_back(AgentConversationToolCall{
    event.runId,
    event.sequence,
    0,
    foundation::ToolId{toolCallId.value()},
    toolSerializedId.value(),
    optionalString(payload, "argumentsJson").value_or("{}"),
    AgentConversationToolCallStatus::Running,
    "",
    std::nullopt
  });
}

void applyToolCallFinished(
  AgentConversationState& state,
  AgentConversationRunState& run,
  const AgentRunEvent& event,
  const Json::Value& payload
) {
  auto toolCallId = requiredString(state, event, payload, "toolCallId");
  auto status = requiredString(state, event, payload, "status");
  if (!toolCallId.has_value() || !status.has_value()) {
    return;
  }

  AgentConversationToolCall* toolCall = findToolCall(run.toolCalls, foundation::ToolId{toolCallId.value()});
  if (toolCall == nullptr) {
    addProjectionDiagnostic(state, event, "agent.tool_call_started_missing", "Tool call finished before start event.");
    return;
  }

  toolCall->finishedSequence = event.sequence;
  toolCall->status = parseToolCallStatus(status.value());
  toolCall->resultJson = optionalString(payload, "resultJson").value_or("{}");
  if (auto observedRevision = optionalString(payload, "observedRevision"); observedRevision.has_value()) {
    toolCall->observedRevision = foundation::RevisionId{observedRevision.value()};
  }
}

void applyDiagnostic(
  AgentConversationState& state,
  AgentConversationRunState& run,
  const AgentRunEvent& event,
  const Json::Value& payload
) {
  auto code = requiredString(state, event, payload, "code");
  auto message = requiredString(state, event, payload, "message");
  if (!code.has_value() || !message.has_value()) {
    return;
  }

  run.diagnostics.push_back(AgentConversationDiagnostic{
    event.runId,
    event.sequence,
    code.value(),
    parseDiagnosticSeverity(optionalString(payload, "severity").value_or("error")),
    message.value()
  });
}

void applyRunFinished(
  AgentConversationState& state,
  AgentConversationRunState& run,
  const AgentRunEvent& event,
  const Json::Value& payload
) {
  auto status = requiredString(state, event, payload, "status");
  if (!status.has_value()) {
    return;
  }

  run.status = parseRunStatus(status.value());
  run.summary = optionalString(payload, "summary").value_or("");
}

void applyDelegatedRunStarted(
  AgentConversationState& state,
  AgentConversationRunState& run,
  const AgentRunEvent& event,
  const Json::Value& payload
) {
  auto delegatedRunId = requiredString(state, event, payload, "runId");
  if (!delegatedRunId.has_value()) {
    return;
  }

  run.delegatedRuns.push_back(AgentDelegatedRunState{
    foundation::RunId{delegatedRunId.value()},
    event.sequence,
    event.sequence,
    AgentRunStatus::Running,
    optionalString(payload, "label").value_or(""),
    optionalString(payload, "summary").value_or("")
  });
}

void applyDelegatedRunUpdated(
  AgentConversationState& state,
  AgentConversationRunState& run,
  const AgentRunEvent& event,
  const Json::Value& payload,
  bool finished
) {
  auto delegatedRunId = requiredString(state, event, payload, "runId");
  if (!delegatedRunId.has_value()) {
    return;
  }

  AgentDelegatedRunState* delegatedRun = findDelegatedRun(run.delegatedRuns, foundation::RunId{delegatedRunId.value()});
  if (delegatedRun == nullptr) {
    addProjectionDiagnostic(state, event, "agent.delegated_run_started_missing", "Delegated run update has no start event.");
    return;
  }

  delegatedRun->updatedSequence = event.sequence;
  delegatedRun->summary = optionalString(payload, "summary").value_or(delegatedRun->summary);
  if (auto status = optionalString(payload, "status"); status.has_value()) {
    delegatedRun->status = parseRunStatus(status.value());
  } else if (finished) {
    delegatedRun->status = AgentRunStatus::Succeeded;
  }
}

} // namespace

AgentConversationState AgentConversationStateProjector::project(
  std::span<const AgentRun> runs,
  std::span<const AgentRunEvent> events
) const {
  AgentConversationState state;

  for (const AgentRunEvent& event : orderedEvents(events)) {
    auto payload = parsePayload(state, event);
    if (!payload.has_value()) {
      continue;
    }

    if (event.kind == AgentRunEventKind::RunStarted) {
      applyRunStarted(state, runs, event, payload.value());
      continue;
    }

    AgentConversationRunState* run = findRunState(state.runs, event.runId);
    if (run == nullptr) {
      addProjectionDiagnostic(state, event, "agent.run_started_missing", "Run event has no start event.");
      continue;
    }

    switch (event.kind) {
      case AgentRunEventKind::RunStarted:
        break;
      case AgentRunEventKind::ModelMessage:
        applyModelMessage(state, *run, event, payload.value());
        break;
      case AgentRunEventKind::ToolCallStarted:
        applyToolCallStarted(state, *run, event, payload.value());
        break;
      case AgentRunEventKind::ToolCallFinished:
        applyToolCallFinished(state, *run, event, payload.value());
        break;
      case AgentRunEventKind::DiagnosticEmitted:
        applyDiagnostic(state, *run, event, payload.value());
        break;
      case AgentRunEventKind::RunFinished:
        applyRunFinished(state, *run, event, payload.value());
        break;
      case AgentRunEventKind::DelegatedRunStarted:
        applyDelegatedRunStarted(state, *run, event, payload.value());
        break;
      case AgentRunEventKind::DelegatedRunUpdated:
        applyDelegatedRunUpdated(state, *run, event, payload.value(), false);
        break;
      case AgentRunEventKind::DelegatedRunFinished:
        applyDelegatedRunUpdated(state, *run, event, payload.value(), true);
        break;
    }
  }

  return state;
}

} // namespace grapple::agent
