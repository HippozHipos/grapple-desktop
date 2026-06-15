#pragma once

#include <grapple/agent/AgentConversationState.hpp>
#include <grapple/agent/AgentRunEventLog.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace grapple::app {

struct NativeStewardMediaPlacementResult {
  storage::ProjectPackageSessionResult packageResult;
  foundation::NodeId clipNodeId;
};

class NativeStewardSession final {
public:
  NativeStewardSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter);

  foundation::Result<storage::ProjectPackageSessionResult> createCameraTransformEffect(
    foundation::NodeId cameraNodeId,
    std::string intent,
    foundation::TimeRange activeRange
  );
  foundation::Result<NativeStewardMediaPlacementResult> placeAssetOnTimeline(
    foundation::AssetId assetId,
    std::optional<foundation::TimeSeconds> duration = std::nullopt
  );
  foundation::Result<storage::ProjectPackageSessionResult> transformClip(
    foundation::NodeId clipNodeId,
    std::string intent
  );
  foundation::Result<storage::ProjectPackageSessionResult> adjustCameraTransformControls(
    foundation::NodeId cameraNodeId,
    std::string intent,
    foundation::TimeRange activeRange
  );

  [[nodiscard]] agent::AgentConversationState conversationState() const;
  [[nodiscard]] const std::vector<agent::AgentRun>& runs() const noexcept;
  [[nodiscard]] const std::vector<agent::AgentRunEvent>& events() const noexcept;
  foundation::Result<void> restoreConversation(
    std::vector<agent::AgentRun> runs,
    std::vector<agent::AgentRunEvent> events
  );

private:
  foundation::Result<foundation::RunId> startRun(
    const project::ProjectSnapshot& snapshot,
    const std::string& title
  );
  foundation::Result<void> appendEvent(
    foundation::RunId runId,
    agent::AgentRunEventKind kind,
    std::string payloadJson
  );
  foundation::Result<void> finishRunWithError(
    const foundation::RunId& runId,
    const foundation::Error& error
  );
  void markRunStatus(const foundation::RunId& runId, agent::AgentRunStatus status);

  NativeProjectSession& project_;
  NativeProjectCommandWriter& commandWriter_;
  std::vector<agent::AgentRun> runs_;
  agent::AgentRunEventLog events_;
  std::int64_t nextRunNumber_ = 1;
  std::int64_t nextSequence_ = 1;
};

} // namespace grapple::app
