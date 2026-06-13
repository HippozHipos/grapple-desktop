#include <grapple/agent/AgentConversationState.hpp>

#include <TestAssert.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

grapple::agent::AgentRun run(
  grapple::foundation::RunId runId,
  std::optional<grapple::foundation::RunId> parentRunId = std::nullopt
) {
  return grapple::agent::AgentRun{
    std::move(runId),
    grapple::foundation::ProjectId{"proj_agent_conversation"},
    std::move(parentRunId),
    grapple::agent::AgentRunStatus::Pending,
    std::chrono::system_clock::now()
  };
}

grapple::agent::AgentRunEvent event(
  grapple::foundation::RunId runId,
  std::int64_t sequence,
  grapple::agent::AgentRunEventKind kind,
  std::string payloadJson
) {
  return grapple::agent::AgentRunEvent{
    std::move(runId),
    sequence,
    kind,
    std::move(payloadJson),
    std::chrono::system_clock::now()
  };
}

bool allUnique(std::vector<std::string_view> values) {
  std::sort(values.begin(), values.end());
  return std::adjacent_find(values.begin(), values.end()) == values.end();
}

} // namespace

int main() {
  using namespace grapple;

  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::RunStarted)} == "run_started");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::ModelMessage)} == "model_message");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::ToolCallStarted)} == "tool_call_started");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::ToolCallFinished)} == "tool_call_finished");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DiagnosticEmitted)} == "diagnostic_emitted");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::RunFinished)} == "run_finished");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DelegatedRunStarted)} == "delegated_run_started");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DelegatedRunUpdated)} == "delegated_run_updated");
  GRAPPLE_REQUIRE(std::string{agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DelegatedRunFinished)} == "delegated_run_finished");
  GRAPPLE_REQUIRE(allUnique({
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::RunStarted),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::ModelMessage),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::ToolCallStarted),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::ToolCallFinished),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DiagnosticEmitted),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::RunFinished),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DelegatedRunStarted),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DelegatedRunUpdated),
    agent::serializedAgentRunEventKind(agent::AgentRunEventKind::DelegatedRunFinished)
  }));

  const agent::AgentConversationStateProjector projector;
  const std::vector<agent::AgentRun> metadataOnlyRuns{
    run(foundation::RunId{"run_parent"}),
    run(foundation::RunId{"run_child"}, foundation::RunId{"run_parent"})
  };
  const agent::AgentConversationState metadataOnly = projector.project(metadataOnlyRuns, {});
  GRAPPLE_REQUIRE(metadataOnly.runs.empty());
  GRAPPLE_REQUIRE(metadataOnly.diagnostics.empty());

  const std::vector<agent::AgentRunEvent> missingStartEvents{
    event(
      foundation::RunId{"run_parent"},
      2,
      agent::AgentRunEventKind::DelegatedRunUpdated,
      R"({"runId":"run_child","status":"running","summary":"working"})"
    )
  };
  const agent::AgentConversationState missingStart = projector.project(metadataOnlyRuns, missingStartEvents);
  GRAPPLE_REQUIRE(missingStart.runs.empty());
  GRAPPLE_REQUIRE(missingStart.diagnostics.size() == 1);
  GRAPPLE_REQUIRE(missingStart.diagnostics[0].code == "agent.run_started_missing");

  const std::vector<agent::AgentRunEvent> missingDelegatedStartEvents{
    event(
      foundation::RunId{"run_parent"},
      1,
      agent::AgentRunEventKind::RunStarted,
      R"({"title":"Parent"})"
    ),
    event(
      foundation::RunId{"run_parent"},
      2,
      agent::AgentRunEventKind::DelegatedRunUpdated,
      R"({"runId":"run_child","status":"running","summary":"working"})"
    )
  };
  const agent::AgentConversationState missingDelegatedStart = projector.project(metadataOnlyRuns, missingDelegatedStartEvents);
  GRAPPLE_REQUIRE(missingDelegatedStart.runs.size() == 1);
  GRAPPLE_REQUIRE(missingDelegatedStart.runs[0].delegatedRuns.empty());
  GRAPPLE_REQUIRE(missingDelegatedStart.diagnostics.size() == 1);
  GRAPPLE_REQUIRE(missingDelegatedStart.diagnostics[0].code == "agent.delegated_run_started_missing");

  const std::vector<agent::AgentRunEvent> events{
    event(
      foundation::RunId{"run_parent"},
      1,
      agent::AgentRunEventKind::RunStarted,
      R"({"title":"Center subject"})"
    ),
    event(
      foundation::RunId{"run_parent"},
      2,
      agent::AgentRunEventKind::ModelMessage,
      R"({"role":"assistant","content":"I will inspect the composition."})"
    ),
    event(
      foundation::RunId{"run_parent"},
      3,
      agent::AgentRunEventKind::ToolCallStarted,
      R"({"toolCallId":"tool_call_1","toolSerializedId":"composition.inspect","argumentsJson":"{}"})"
    ),
    event(
      foundation::RunId{"run_parent"},
      4,
      agent::AgentRunEventKind::ToolCallFinished,
      R"({"toolCallId":"tool_call_1","status":"succeeded","resultJson":"{\"revision\":\"rev_1\"}"})"
    ),
    event(
      foundation::RunId{"run_parent"},
      5,
      agent::AgentRunEventKind::DiagnosticEmitted,
      R"({"code":"agent.note","severity":"warning","message":"Need camera id."})"
    ),
    event(
      foundation::RunId{"run_parent"},
      6,
      agent::AgentRunEventKind::DelegatedRunStarted,
      R"({"runId":"run_child","label":"Find camera","summary":"starting"})"
    ),
    event(
      foundation::RunId{"run_parent"},
      7,
      agent::AgentRunEventKind::DelegatedRunUpdated,
      R"({"runId":"run_child","status":"running","summary":"reading graph"})"
    ),
    event(
      foundation::RunId{"run_parent"},
      8,
      agent::AgentRunEventKind::DelegatedRunFinished,
      R"({"runId":"run_child","status":"succeeded","summary":"camera found"})"
    ),
    event(
      foundation::RunId{"run_parent"},
      9,
      agent::AgentRunEventKind::RunFinished,
      R"({"status":"succeeded","summary":"Effect created."})"
    )
  };

  const agent::AgentConversationState state = projector.project(metadataOnlyRuns, events);
  GRAPPLE_REQUIRE(state.diagnostics.empty());
  GRAPPLE_REQUIRE(state.runs.size() == 1);
  const agent::AgentConversationRunState& parent = state.runs[0];
  GRAPPLE_REQUIRE(parent.runId == foundation::RunId{"run_parent"});
  GRAPPLE_REQUIRE(parent.projectId == foundation::ProjectId{"proj_agent_conversation"});
  GRAPPLE_REQUIRE(!parent.parentRunId.has_value());
  GRAPPLE_REQUIRE(parent.status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(parent.title == "Center subject");
  GRAPPLE_REQUIRE(parent.summary == "Effect created.");

  GRAPPLE_REQUIRE(parent.messages.size() == 1);
  GRAPPLE_REQUIRE(parent.messages[0].role == "assistant");
  GRAPPLE_REQUIRE(parent.messages[0].content == "I will inspect the composition.");

  GRAPPLE_REQUIRE(parent.toolCalls.size() == 1);
  GRAPPLE_REQUIRE(parent.toolCalls[0].toolCallId == foundation::ToolId{"tool_call_1"});
  GRAPPLE_REQUIRE(parent.toolCalls[0].toolSerializedId == "composition.inspect");
  GRAPPLE_REQUIRE(parent.toolCalls[0].status == agent::AgentConversationToolCallStatus::Succeeded);
  GRAPPLE_REQUIRE(parent.toolCalls[0].resultJson == R"({"revision":"rev_1"})");

  GRAPPLE_REQUIRE(parent.diagnostics.size() == 1);
  GRAPPLE_REQUIRE(parent.diagnostics[0].code == "agent.note");
  GRAPPLE_REQUIRE(parent.diagnostics[0].severity == agent::DiagnosticSeverity::Warning);
  GRAPPLE_REQUIRE(parent.diagnostics[0].message == "Need camera id.");

  GRAPPLE_REQUIRE(parent.delegatedRuns.size() == 1);
  GRAPPLE_REQUIRE(parent.delegatedRuns[0].runId == foundation::RunId{"run_child"});
  GRAPPLE_REQUIRE(parent.delegatedRuns[0].label == "Find camera");
  GRAPPLE_REQUIRE(parent.delegatedRuns[0].status == agent::AgentRunStatus::Succeeded);
  GRAPPLE_REQUIRE(parent.delegatedRuns[0].summary == "camera found");

  return 0;
}
