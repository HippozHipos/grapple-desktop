#include <grapple/agent/AgentBridge.hpp>
#include <grapple/agent/AgentConversationState.hpp>
#include <grapple/agent/ProjectTools.hpp>
#include <grapple/project/ProjectController.hpp>

#include <TestAssert.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {

grapple::agent::AgentRunEvent runStartedEvent() {
  return grapple::agent::AgentRunEvent{
    grapple::foundation::RunId{"run_bridge"},
    0,
    grapple::agent::AgentRunEventKind::RunStarted,
    R"({"title":"Inspect project"})",
    std::chrono::system_clock::now()
  };
}

class TestProjectIdAllocator final : public grapple::project::IProjectIdAllocator {
public:
  grapple::foundation::CommandId nextCommandId(const std::string& stem) override {
    return grapple::foundation::CommandId{"cmd_test_" + stem};
  }

  grapple::foundation::AssetId nextAssetId(const std::string& stem) override {
    return grapple::foundation::AssetId{"asset_test_" + stem};
  }

  grapple::foundation::NodeId nextNodeId(const std::string& stem) override {
    return grapple::foundation::NodeId{"node_test_" + stem};
  }

  grapple::foundation::EdgeId nextEdgeId(const std::string& stem) override {
    return grapple::foundation::EdgeId{"edge_test_" + stem};
  }
};

} // namespace

int main() {
  using namespace grapple;

  agent::AgentRunEventLog log;
  const auto invalidSequence = log.append(agent::AgentRunEvent{
    foundation::RunId{"run_bridge"},
    0,
    agent::AgentRunEventKind::RunStarted,
    R"({"title":"Invalid"})",
    std::chrono::system_clock::now()
  });
  GRAPPLE_REQUIRE(!invalidSequence);
  GRAPPLE_REQUIRE(invalidSequence.error().code == "agent.run_event_sequence_invalid");

  agent::AgentToolRegistry registry;
  const auto registered = registry.registerTool(agent::makeProjectInspectTool());
  GRAPPLE_REQUIRE(registered);

  project::ProjectController project{
    project::createEmptyProject(foundation::ProjectId{"proj_bridge"}, "Bridge Project")
  };
  TestProjectIdAllocator ids;
  agent::AgentToolContext context{project, project, ids};
  std::int64_t nextSequence = 1;
  agent::AgentBridge bridge{registry, context, log, nextSequence};

  const auto result = bridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    foundation::RunId{"run_bridge"},
    foundation::ProjectId{"proj_bridge"},
    foundation::RevisionId{"rev_0"},
    foundation::ToolId{"tool_call_project_inspect"},
    "project.inspect",
    "{}"
  });
  GRAPPLE_REQUIRE(result);
  GRAPPLE_REQUIRE(result.value().status == agent::ToolResultStatus::Succeeded);
  GRAPPLE_REQUIRE(result.value().observedRevision == foundation::RevisionId{"rev_0"});

  GRAPPLE_REQUIRE(log.records().size() == 2);
  GRAPPLE_REQUIRE(log.records()[0].runId == foundation::RunId{"run_bridge"});
  GRAPPLE_REQUIRE(log.records()[0].sequence == 1);
  GRAPPLE_REQUIRE(log.records()[0].kind == agent::AgentRunEventKind::ToolCallStarted);
  GRAPPLE_REQUIRE(log.records()[0].payloadJson.find("\"toolSerializedId\":\"project.inspect\"") != std::string::npos);
  GRAPPLE_REQUIRE(log.records()[1].sequence == 2);
  GRAPPLE_REQUIRE(log.records()[1].kind == agent::AgentRunEventKind::ToolCallFinished);
  GRAPPLE_REQUIRE(log.records()[1].payloadJson.find("\"status\":\"succeeded\"") != std::string::npos);

  std::vector<agent::AgentRunEvent> conversationEvents{runStartedEvent()};
  conversationEvents.insert(conversationEvents.end(), log.records().begin(), log.records().end());
  const std::vector<agent::AgentRun> runs{
    agent::AgentRun{
      foundation::RunId{"run_bridge"},
      foundation::ProjectId{"proj_bridge"},
      std::nullopt,
      agent::AgentRunStatus::Running,
      std::chrono::system_clock::now()
    }
  };
  const agent::AgentConversationStateProjector projector;
  const agent::AgentConversationState state = projector.project(runs, conversationEvents);
  GRAPPLE_REQUIRE(state.diagnostics.empty());
  GRAPPLE_REQUIRE(state.runs.size() == 1);
  GRAPPLE_REQUIRE(state.runs[0].toolCalls.size() == 1);
  GRAPPLE_REQUIRE(state.runs[0].toolCalls[0].toolCallId == foundation::ToolId{"tool_call_project_inspect"});
  GRAPPLE_REQUIRE(state.runs[0].toolCalls[0].toolSerializedId == "project.inspect");
  GRAPPLE_REQUIRE(state.runs[0].toolCalls[0].status == agent::AgentConversationToolCallStatus::Succeeded);

  agent::AgentRunEventLog missingToolLog;
  std::int64_t missingToolNextSequence = 1;
  agent::AgentBridge missingToolBridge{registry, context, missingToolLog, missingToolNextSequence};
  const auto missingTool = missingToolBridge.dispatchToolCall(agent::AgentToolDispatchRequest{
    foundation::RunId{"run_bridge"},
    foundation::ProjectId{"proj_bridge"},
    foundation::RevisionId{"rev_0"},
    foundation::ToolId{"tool_call_missing"},
    "missing.tool",
    "{}"
  });
  GRAPPLE_REQUIRE(!missingTool);
  GRAPPLE_REQUIRE(missingTool.error().code == "agent.tool_missing");
  GRAPPLE_REQUIRE(missingToolLog.records().size() == 2);
  GRAPPLE_REQUIRE(missingToolLog.records()[1].kind == agent::AgentRunEventKind::ToolCallFinished);
  GRAPPLE_REQUIRE(missingToolLog.records()[1].payloadJson.find("\"status\":\"failed\"") != std::string::npos);

  return 0;
}
