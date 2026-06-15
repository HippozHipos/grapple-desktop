#include <grapple/agent/AgentConversationState.hpp>
#include <grapple/agent/AgentRunEventSerializer.hpp>
#include <grapple/agent/AgentRunSerializer.hpp>

#include <TestAssert.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {

std::chrono::system_clock::time_point atMs(std::int64_t milliseconds) {
  return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
}

grapple::agent::AgentRunEvent event(
  std::int64_t sequence,
  grapple::agent::AgentRunEventKind kind,
  std::string payloadJson,
  std::int64_t createdAtMs
) {
  return grapple::agent::AgentRunEvent{
    grapple::foundation::RunId{"run_serialized"},
    sequence,
    kind,
    std::move(payloadJson),
    atMs(createdAtMs)
  };
}

} // namespace

int main() {
  using namespace grapple;

  const std::vector<agent::AgentRunEvent> events{
    event(1, agent::AgentRunEventKind::RunStarted, R"({"title":"Center subject"})", 1000),
    event(
      2,
      agent::AgentRunEventKind::ToolCallStarted,
      R"({"toolCallId":"tool_1","toolSerializedId":"composition.inspect","argumentsJson":"{}"})",
      1100
    ),
    event(
      3,
      agent::AgentRunEventKind::ToolCallFinished,
      R"({"toolCallId":"tool_1","status":"succeeded","resultJson":"{\"revision\":\"rev_1\"}","observedRevision":"rev_1"})",
      1200
    ),
    event(4, agent::AgentRunEventKind::RunFinished, R"({"status":"succeeded","summary":"Done."})", 1300)
  };

  const std::string serialized = agent::serializeCanonicalAgentRunEvents(events);
  const std::string expected =
    R"([{"runId":"run_serialized","sequence":1,"kind":"run_started","payloadJson":"{\"title\":\"Center subject\"}","createdAtMs":1000},{"runId":"run_serialized","sequence":2,"kind":"tool_call_started","payloadJson":"{\"toolCallId\":\"tool_1\",\"toolSerializedId\":\"composition.inspect\",\"argumentsJson\":\"{}\"}","createdAtMs":1100},{"runId":"run_serialized","sequence":3,"kind":"tool_call_finished","payloadJson":"{\"toolCallId\":\"tool_1\",\"status\":\"succeeded\",\"resultJson\":\"{\\\"revision\\\":\\\"rev_1\\\"}\",\"observedRevision\":\"rev_1\"}","createdAtMs":1200},{"runId":"run_serialized","sequence":4,"kind":"run_finished","payloadJson":"{\"status\":\"succeeded\",\"summary\":\"Done.\"}","createdAtMs":1300}])";
  GRAPPLE_REQUIRE(serialized == expected);

  const auto deserialized = agent::deserializeCanonicalAgentRunEvents(serialized);
  GRAPPLE_REQUIRE(deserialized);
  GRAPPLE_REQUIRE(deserialized.value().size() == events.size());
  GRAPPLE_REQUIRE(agent::serializeCanonicalAgentRunEvents(deserialized.value()) == serialized);

  const std::vector<agent::AgentRun> runs{
    agent::AgentRun{
      foundation::RunId{"run_serialized"},
      foundation::ProjectId{"proj_serialized"},
      std::nullopt,
      agent::AgentRunStatus::Running,
      atMs(900)
    }
  };
  const agent::AgentConversationStateProjector projector;
  const agent::AgentConversationState state = projector.project(runs, deserialized.value());
  GRAPPLE_REQUIRE(state.diagnostics.empty());
  GRAPPLE_REQUIRE(state.runs.size() == 1);
  GRAPPLE_REQUIRE(state.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(state.runs[0].toolCalls[0].toolSerializedId == "composition.inspect");
  GRAPPLE_REQUIRE(state.runs[0].toolCalls[0].observedRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(state.runs[0].status == agent::AgentRunStatus::Succeeded);

  const std::string serializedRuns = agent::serializeCanonicalAgentRuns(runs);
  const std::string expectedRuns =
    R"([{"id":"run_serialized","projectId":"proj_serialized","parentRunId":null,"status":"running","createdAtMs":900}])";
  GRAPPLE_REQUIRE(serializedRuns == expectedRuns);
  const auto deserializedRuns = agent::deserializeCanonicalAgentRuns(serializedRuns);
  GRAPPLE_REQUIRE(deserializedRuns);
  GRAPPLE_REQUIRE(agent::serializeCanonicalAgentRuns(deserializedRuns.value()) == serializedRuns);

  const auto invalidRunStatus = agent::deserializeCanonicalAgentRuns(
    R"([{"id":"run_serialized","projectId":"proj_serialized","parentRunId":null,"status":"missing","createdAtMs":900}])"
  );
  GRAPPLE_REQUIRE(!invalidRunStatus);
  GRAPPLE_REQUIRE(invalidRunStatus.error().code == "agent.run_status_invalid");

  const auto invalidKind = agent::deserializeCanonicalAgentRunEvents(
    R"([{"runId":"run_serialized","sequence":1,"kind":"missing_kind","payloadJson":"{}","createdAtMs":1000}])"
  );
  GRAPPLE_REQUIRE(!invalidKind);
  GRAPPLE_REQUIRE(invalidKind.error().code == "agent.run_event_kind_invalid");

  const auto duplicateSequence = agent::deserializeCanonicalAgentRunEvents(
    R"([{"runId":"run_serialized","sequence":1,"kind":"run_started","payloadJson":"{}","createdAtMs":1000},{"runId":"run_serialized","sequence":1,"kind":"run_finished","payloadJson":"{}","createdAtMs":1100}])"
  );
  GRAPPLE_REQUIRE(!duplicateSequence);
  GRAPPLE_REQUIRE(duplicateSequence.error().code == "agent.run_event_log_json_invalid");

  return 0;
}
