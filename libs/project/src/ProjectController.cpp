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
  const std::int64_t beforeRevisionNumber = document_.revisionNumber;
  ProjectDocument nextDocument = document_;
  ProjectController nextController{nextDocument};

  const auto payloadResult = nextController.applyPayload(command.payload);
  if (!payloadResult) {
    return payloadResult.error();
  }

  nextController.document_.revisionNumber = beforeRevisionNumber + 1;
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
      if constexpr (std::is_same_v<Command, RegisterAssetCommand>) {
        return handleRegisterAsset(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateCompositionCommand>) {
        return handleCreateComposition(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateTrackCommand>) {
        return handleCreateTrack(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateClipCommand>) {
        return handleCreateClip(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateCameraCommand>) {
        return handleCreateCamera(typedCommand);
      } else if constexpr (std::is_same_v<Command, CreateEffectCommand>) {
        return handleCreateEffect(typedCommand);
      } else if constexpr (std::is_same_v<Command, SetEffectParamsCommand>) {
        return handleSetEffectParams(typedCommand);
      } else if constexpr (std::is_same_v<Command, RestoreSnapshotCommand>) {
        return handleRestoreSnapshot(typedCommand);
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

foundation::Result<void> ProjectController::handleRegisterAsset(const RegisterAssetCommand& command) {
  return document_.assets.registerAsset(command.asset);
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

foundation::Result<void> ProjectController::handleCreateClip(const CreateClipCommand& command) {
  const graph::GraphNode* track = document_.graph.findNode(command.trackNodeId);
  if (track == nullptr || track->kind != graph::NodeKind::Track) {
    return foundation::Error{"project.track_missing", "Clip must be created inside an existing track."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Clip,
    command.payload,
    true
  });
  if (!nodeResult) {
    return nodeResult;
  }

  return document_.graph.addEdge(graph::GraphEdge{
    command.containmentEdgeId,
    graph::EdgeKind::Contains,
    command.trackNodeId,
    command.nodeId,
    true
  });
}

foundation::Result<void> ProjectController::handleCreateCamera(const CreateCameraCommand& command) {
  const graph::GraphNode* composition = document_.graph.findNode(command.compositionNodeId);
  if (composition == nullptr || composition->kind != graph::NodeKind::Composition) {
    return foundation::Error{"project.composition_missing", "Camera must be created inside an existing composition."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Camera,
    command.payload,
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

foundation::Result<void> ProjectController::handleCreateEffect(const CreateEffectCommand& command) {
  if (!document_.graph.hasNode(command.targetNodeId)) {
    return foundation::Error{"project.effect_target_missing", "Effect target node must exist."};
  }

  auto nodeResult = document_.graph.addNode(graph::GraphNode{
    command.nodeId,
    graph::NodeKind::Effect,
    command.payload,
    true
  });
  if (!nodeResult) {
    return nodeResult;
  }

  return document_.graph.addEdge(graph::GraphEdge{
    command.targetEdgeId,
    graph::EdgeKind::Targets,
    command.nodeId,
    command.targetNodeId,
    true
  });
}

foundation::Result<void> ProjectController::handleSetEffectParams(const SetEffectParamsCommand& command) {
  const graph::GraphNode* effect = document_.graph.findNode(command.effectNodeId);
  if (effect == nullptr || effect->kind != graph::NodeKind::Effect) {
    return foundation::Error{"project.effect_missing", "Effect params can only be set on an existing effect node."};
  }

  const auto* payload = std::get_if<timeline::EffectPayload>(&effect->payload);
  if (payload == nullptr) {
    return foundation::Error{"project.effect_payload_invalid", "Effect node must carry an effect payload."};
  }

  timeline::EffectPayload updated = *payload;
  updated.params = command.params;
  return document_.graph.replaceNodePayload(command.effectNodeId, std::move(updated));
}

foundation::Result<void> ProjectController::handleRestoreSnapshot(const RestoreSnapshotCommand& command) {
  if (!command.snapshotId) {
    return foundation::Error{"project.snapshot_id_empty", "Restore snapshot id must not be empty."};
  }

  if (command.document.info.id != document_.info.id) {
    return foundation::Error{"project.restore_project_id_mismatch", "Restore snapshot document must match the open project."};
  }

  document_ = command.document;
  return {};
}

ProjectDocument createEmptyProject(foundation::ProjectId projectId, std::string name) {
  return ProjectDocument{
    ProjectInfo{std::move(projectId), std::move(name)},
    makeRevisionId(0),
    0,
    asset::AssetCatalog{},
    graph::GraphDocument{}
  };
}

} // namespace grapple::project
