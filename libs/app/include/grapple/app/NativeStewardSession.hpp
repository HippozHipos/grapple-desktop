#pragma once

#include <grapple/agent/AgentConversationState.hpp>
#include <grapple/agent/AgentRunEventLog.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/app/NativeStewardPlanner.hpp>
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

struct NativeStewardTextClipResult {
  storage::ProjectPackageSessionResult packageResult;
  foundation::NodeId textClipNodeId;
};

struct NativeStewardNoteResult {
  storage::ProjectPackageSessionResult packageResult;
  foundation::NodeId noteNodeId;
};

struct NativeStewardTrackResult {
  storage::ProjectPackageSessionResult packageResult;
  foundation::NodeId trackNodeId;
};

struct NativeStewardCameraResult {
  storage::ProjectPackageSessionResult packageResult;
  foundation::NodeId cameraNodeId;
};

class NativeStewardSession final {
public:
  NativeStewardSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter);

  foundation::Result<storage::ProjectPackageSessionResult> createCameraTransformEffect(
    foundation::NodeId cameraNodeId,
    std::string intent,
    foundation::TimeRange activeRange
  );
  foundation::Result<storage::ProjectPackageSessionResult> undoLastEdit(std::string intent);
  foundation::Result<storage::ProjectPackageSessionResult> redoLastEdit(std::string intent);
  foundation::Result<storage::ProjectPackageSessionResult> deleteCameraTransformEffect(
    foundation::NodeId cameraNodeId,
    std::string intent
  );
  foundation::Result<NativeStewardMediaPlacementResult> placeAssetOnTimeline(
    foundation::AssetId assetId,
    std::optional<foundation::TimeSeconds> duration = std::nullopt
  );
  foundation::Result<NativeStewardTextClipResult> createTextClip(
    std::string intent,
    foundation::TimeSeconds start
  );
  foundation::Result<NativeStewardNoteResult> createNote(std::string intent);
  foundation::Result<NativeStewardTrackResult> createTrack(std::string intent);
  foundation::Result<NativeStewardCameraResult> createCamera();
  foundation::Result<storage::ProjectPackageSessionResult> updateCamera(
    foundation::NodeId cameraNodeId,
    std::string intent
  );
  foundation::Result<storage::ProjectPackageSessionResult> editClip(
    foundation::NodeId clipNodeId,
    std::string intent
  );
  foundation::Result<storage::ProjectPackageSessionResult> createClipTintEffect(
    foundation::NodeId clipNodeId,
    std::string intent,
    foundation::TimeRange activeRange
  );
  foundation::Result<storage::ProjectPackageSessionResult> adjustClipTintControls(
    foundation::NodeId clipNodeId,
    std::string intent
  );
  foundation::Result<storage::ProjectPackageSessionResult> deleteClip(
    foundation::NodeId clipNodeId,
    std::string intent
  );
  foundation::Result<storage::ProjectPackageSessionResult> deleteTrack(
    foundation::NodeId trackNodeId,
    std::string intent
  );
  foundation::Result<storage::ProjectPackageSessionResult> editTextClip(
    foundation::NodeId clipNodeId,
    std::string intent
  );
  foundation::Result<storage::ProjectPackageSessionResult> editNote(
    foundation::NodeId noteNodeId,
    std::string intent
  );
  [[nodiscard]] bool clipEditIntentTargetsClip(const std::string& intent) const;
  [[nodiscard]] bool clipTintIntentTargetsClip(const std::string& intent) const;
  [[nodiscard]] bool clipDeleteIntentTargetsClip(const std::string& intent) const;
  [[nodiscard]] bool cameraUpdateIntentTargetsCamera(const std::string& intent) const;
  [[nodiscard]] bool trackCreateIntentTargetsTrack(const std::string& intent) const;
  [[nodiscard]] bool trackDeleteIntentTargetsTrack(const std::string& intent) const;
  [[nodiscard]] bool textClipIntentTargetsText(const std::string& intent) const;
  [[nodiscard]] bool textClipEditIntentTargetsTextClip(const std::string& intent) const;
  [[nodiscard]] bool noteIntentTargetsNote(const std::string& intent) const;
  [[nodiscard]] bool noteEditIntentTargetsNote(const std::string& intent) const;
  [[nodiscard]] bool undoIntentTargetsLastEdit(const std::string& intent) const;
  [[nodiscard]] bool redoIntentTargetsLastUndoneEdit(const std::string& intent) const;
  [[nodiscard]] bool historyIntentTargetsEdit(const std::string& intent) const;
  [[nodiscard]] bool cameraTransformDeleteIntentTargetsCameraControls(const std::string& intent) const;
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
  NativeStewardPlanner planner_;
  std::vector<agent::AgentRun> runs_;
  agent::AgentRunEventLog events_;
  std::int64_t nextRunNumber_ = 1;
  std::int64_t nextSequence_ = 1;
};

} // namespace grapple::app
