#include <grapple/project/ProjectController.hpp>

#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <string>

namespace grapple::project {

namespace {

foundation::RevisionId makeRevisionId(std::int64_t revisionNumber) {
  return foundation::RevisionId{"rev_" + std::to_string(revisionNumber)};
}

} // namespace

ProjectController::ProjectController(ProjectDocument document)
  : document_(std::move(document)) {}

foundation::Result<ProjectCommandResult> ProjectController::apply(const ProjectCommandEnvelope& command) {
  if (command.projectId != document_.info.id) {
    return foundation::Error{"project.id_mismatch", "Command project id does not match the open project."};
  }

  if (command.expectedRevision != document_.revision) {
    return foundation::Error{"project.expected_revision_mismatch", "Command expected revision does not match current revision."};
  }

  const foundation::RevisionId beforeRevision = document_.revision;
  ProjectDocument nextDocument = document_;
  ProjectController nextController{nextDocument};

  const auto payloadResult = nextController.applyPayload(command.payload);
  if (!payloadResult) {
    return payloadResult.error();
  }

  nextController.document_.revisionNumber += 1;
  nextController.document_.revision = nextController.nextRevisionId();
  document_ = std::move(nextController.document_);

  ProjectCommandResult result{
    command.id,
    beforeRevision,
    document_.revision,
    {
      ProjectCommandAppliedEvent{command.id, beforeRevision, document_.revision},
      ProjectChangedEvent{document_.info.id, beforeRevision, document_.revision}
    },
    {}
  };

  return result;
}

foundation::Result<ProjectSnapshot> ProjectController::snapshot() const {
  return ProjectSnapshot{document_};
}

foundation::Result<ProjectQueryResult> ProjectController::query(const ProjectQuery& query) const {
  return readQuery(query);
}

foundation::RevisionId ProjectController::nextRevisionId() const {
  return makeRevisionId(document_.revisionNumber);
}

foundation::Result<void> ProjectController::applyPayload(const ProjectCommand& payload) {
  return std::visit(
    [&](const auto& typedCommand) -> foundation::Result<void> {
      using Command = std::decay_t<decltype(typedCommand)>;
      if constexpr (std::is_same_v<Command, CreateCompositionCommand>) {
        return handleCreateComposition(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateTrackCommand>) {
        return handleCreateTrack(typedCommand);
      }
    },
    payload
  );
}

foundation::Result<ProjectQueryResult> ProjectController::readQuery(const ProjectQuery& query) const {
  return std::visit(
    [&](const auto& typedQuery) -> foundation::Result<ProjectQueryResult> {
      using Query = std::decay_t<decltype(typedQuery)>;
      if constexpr (std::is_same_v<Query, GetProjectSnapshotQuery>) {
        return ProjectQueryResult{ProjectSnapshotResult{ProjectSnapshot{document_}}};
      } else if constexpr (std::is_same_v<Query, GetGraphQuery>) {
        return ProjectQueryResult{GraphResult{document_.graph}};
      }
    },
    query
  );
}

foundation::Result<void> ProjectController::handleCreateComposition(const CreateCompositionCommand& command) {
  graph::GraphNode node{
    command.nodeId,
    graph::NodeKind::Composition,
    timeline::CompositionPayload{command.name},
    true
  };

  return document_.graph.addNode(std::move(node));
}

foundation::Result<void> ProjectController::handleCreateTrack(const CreateTrackCommand& command) {
  const graph::GraphNode* composition = document_.graph.findNode(command.compositionNodeId);
  if (composition == nullptr || composition->kind != graph::NodeKind::Composition) {
    return foundation::Error{"project.composition_missing", "Track must be created inside an existing composition."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Track,
    timeline::TrackPayload{command.name},
    true
  });
  if (!nodeResult) {
    return nodeResult;
  }

  return document_.graph.addEdge(graph::GraphEdge{
    command.containmentEdgeId,
    graph::EdgeKind::Contains,
    command.compositionNodeId,
    command.nodeId,
    true
  });
}

ProjectDocument createEmptyProject(foundation::ProjectId projectId, std::string name) {
  return ProjectDocument{
    ProjectInfo{std::move(projectId), std::move(name)},
    makeRevisionId(0),
    0,
    graph::GraphDocument{}
  };
}

} // namespace grapple::project
