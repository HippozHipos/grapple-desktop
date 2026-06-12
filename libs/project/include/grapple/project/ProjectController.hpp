#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectCommandResult.hpp>
#include <grapple/project/ProjectDocument.hpp>

namespace grapple::project {

class ProjectController {
public:
  explicit ProjectController(ProjectDocument document);

  foundation::Result<ProjectCommandResult> apply(const ProjectCommandEnvelope& command);
  foundation::Result<ProjectSnapshot> snapshot() const;

private:
  [[nodiscard]] foundation::RevisionId nextRevisionId() const;
  foundation::Result<void> applyPayload(const ProjectCommand& payload);
  foundation::Result<void> handleCreateComposition(const CreateCompositionCommand& command);
  foundation::Result<void> handleCreateTrack(const CreateTrackCommand& command);

  ProjectDocument document_;
};

ProjectDocument createEmptyProject(foundation::ProjectId projectId, std::string name);

} // namespace grapple::project

