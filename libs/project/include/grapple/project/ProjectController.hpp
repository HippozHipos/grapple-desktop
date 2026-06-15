#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectCommandResult.hpp>
#include <grapple/project/ProjectCommandService.hpp>
#include <grapple/project/ProjectDocument.hpp>
#include <grapple/project/ProjectQuery.hpp>
#include <grapple/project/ProjectSnapshot.hpp>

namespace grapple::project {

class ProjectController : public IProjectCommandService, public IProjectQueryService {
public:
  explicit ProjectController(ProjectDocument document);

  foundation::Result<ProjectCommandResult> apply(const ProjectCommandEnvelope& command) override;
  foundation::Result<ProjectSnapshot> snapshot() const;
  foundation::Result<ProjectQueryResult> query(const ProjectQuery& query) const override;

private:
  [[nodiscard]] foundation::RevisionId nextRevisionId() const;
  foundation::Result<void> validateCommand(const ProjectCommandEnvelope& command) const;
  foundation::Result<void> applyPayload(const ProjectCommand& payload);
  foundation::Result<ProjectQueryResult> readQuery(const ProjectQuery& query) const;
  foundation::Result<void> handleRegisterAsset(const RegisterAssetCommand& command);
  foundation::Result<void> handleCreateComposition(const CreateCompositionCommand& command);
  foundation::Result<void> handleCreateTrack(const CreateTrackCommand& command);
  foundation::Result<void> handleDeleteTrack(const DeleteTrackCommand& command);
  foundation::Result<void> handleAddMediaToTimeline(const AddMediaToTimelineCommand& command);
  foundation::Result<void> handleCreateClip(const CreateClipCommand& command);
  foundation::Result<void> handleMoveClip(const MoveClipCommand& command);
  foundation::Result<void> handleTrimClip(const TrimClipCommand& command);
  foundation::Result<void> handleUpdateClip(const UpdateClipCommand& command);
  foundation::Result<void> handleDeleteClip(const DeleteClipCommand& command);
  foundation::Result<void> handleCreateCamera(const CreateCameraCommand& command);
  foundation::Result<void> handleUpdateCamera(const UpdateCameraCommand& command);
  foundation::Result<void> handleCreateEffect(const CreateEffectCommand& command);
  foundation::Result<void> handleDeleteEffect(const DeleteEffectCommand& command);
  foundation::Result<void> handleConnectPorts(const ConnectPortsCommand& command);
  foundation::Result<void> handleDisconnectPorts(const DisconnectPortsCommand& command);
  foundation::Result<void> handleUpdateEffectParamValue(const UpdateEffectParamValueCommand& command);
  foundation::Result<void> handleUpsertEffectParamKeyframe(const UpsertEffectParamKeyframeCommand& command);
  foundation::Result<void> handleDeleteEffectParamKeyframe(const DeleteEffectParamKeyframeCommand& command);
  foundation::Result<void> handleCreateNote(const CreateNoteCommand& command);
  foundation::Result<void> handleUpdateNote(const UpdateNoteCommand& command);
  foundation::Result<void> handleRestoreSnapshot(const RestoreSnapshotCommand& command);

  ProjectDocument document_;
};

ProjectDocument createEmptyProject(foundation::ProjectId projectId, std::string name);

} // namespace grapple::project
