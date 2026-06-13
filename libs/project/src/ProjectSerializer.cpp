#include <grapple/project/ProjectSerializer.hpp>

#include <grapple/asset/AssetSerializer.hpp>
#include <grapple/foundation/Json.hpp>
#include <grapple/graph/GraphSerializer.hpp>
#include <grapple/timeline/TimelineSerializer.hpp>

#include <sstream>
#include <type_traits>

namespace grapple::project {

namespace {

void writeIdProperty(std::ostringstream& stream, const char* key, const std::string& value) {
  foundation::writeJsonStringProperty(stream, key, value);
}

void writeProjectSettings(std::ostringstream& stream, const ProjectSettings& settings) {
  stream << "{\"defaultDuration\":";
  if (settings.defaultDuration.has_value()) {
    stream << settings.defaultDuration->value;
  } else {
    stream << "null";
  }
  stream << '}';
}

void writeNoteCommandPayload(
  std::ostringstream& stream,
  foundation::NodeId nodeId,
  const timeline::NotePayload& payload
) {
  writeIdProperty(stream, "nodeId", nodeId.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "title", payload.title);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "markdown", payload.markdown);
}

} // namespace

std::string serializeCanonicalProjectDocument(const ProjectDocument& document) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "projectId", document.info.id.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "name", document.info.name);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "revision", document.revision.value());
  stream << ",\"revisionNumber\":" << document.revisionNumber;
  stream << ",\"settings\":";
  writeProjectSettings(stream, document.settings);
  stream << ",\"assets\":" << asset::serializeCanonicalAssetCatalog(document.assets);
  stream << ",\"graph\":" << graph::serializeCanonicalGraph(document.graph);
  stream << '}';
  return stream.str();
}

std::string serializeCanonicalProjectSnapshot(const ProjectSnapshot& snapshot) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "projectId", snapshot.info.id.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "name", snapshot.info.name);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "revision", snapshot.revision.value());
  stream << ",\"revisionNumber\":" << snapshot.revisionNumber;
  stream << ",\"settings\":";
  writeProjectSettings(stream, snapshot.settings);
  stream << ",\"assets\":" << asset::serializeCanonicalAssetCatalog(snapshot.assets);
  stream << ",\"graph\":" << graph::serializeCanonicalGraph(snapshot.graph);
  stream << '}';
  return stream.str();
}

std::string serializeCanonicalCommandPayload(const ProjectCommand& command) {
  return std::visit(
    [](const auto& typedCommand) -> std::string {
      using Command = std::decay_t<decltype(typedCommand)>;
      std::ostringstream stream;
      stream << '{';

      if constexpr (std::is_same_v<Command, RegisterAssetCommand>) {
        stream << "\"asset\":" << asset::serializeCanonicalAsset(typedCommand.asset);
      } else if constexpr (std::is_same_v<Command, CreateCompositionCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ',';
        foundation::writeJsonStringProperty(stream, "name", typedCommand.name);
      } else if constexpr (std::is_same_v<Command, CreateTrackCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ',';
        writeIdProperty(stream, "compositionNodeId", typedCommand.compositionNodeId.value());
        stream << ',';
        writeIdProperty(stream, "containmentEdgeId", typedCommand.containmentEdgeId.value());
        stream << ',';
        foundation::writeJsonStringProperty(stream, "name", typedCommand.name);
        stream << ",\"order\":" << typedCommand.order;
      } else if constexpr (std::is_same_v<Command, CreateClipCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ',';
        writeIdProperty(stream, "trackNodeId", typedCommand.trackNodeId.value());
        stream << ',';
        writeIdProperty(stream, "containmentEdgeId", typedCommand.containmentEdgeId.value());
        stream << ",\"payload\":" << timeline::serializeCanonicalClipPayload(typedCommand.payload);
        stream << ",\"order\":" << typedCommand.order;
      } else if constexpr (std::is_same_v<Command, MoveClipCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ",\"newStart\":" << typedCommand.newStart.value;
      } else if constexpr (std::is_same_v<Command, TrimClipCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ",\"timelineRange\":" << timeline::serializeCanonicalTimeRange(typedCommand.timelineRange);
        stream << ",\"sourceRange\":" << timeline::serializeCanonicalTimeRange(typedCommand.sourceRange);
      } else if constexpr (std::is_same_v<Command, UpdateClipCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ",\"payload\":" << timeline::serializeCanonicalClipPayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, DeleteClipCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
      } else if constexpr (std::is_same_v<Command, CreateCameraCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ',';
        writeIdProperty(stream, "compositionNodeId", typedCommand.compositionNodeId.value());
        stream << ',';
        writeIdProperty(stream, "containmentEdgeId", typedCommand.containmentEdgeId.value());
        stream << ",\"payload\":" << timeline::serializeCanonicalCameraPayload(typedCommand.payload);
        stream << ",\"order\":" << typedCommand.order;
      } else if constexpr (std::is_same_v<Command, UpdateCameraCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ",\"payload\":" << timeline::serializeCanonicalCameraPayload(typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, CreateEffectCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
        stream << ',';
        writeIdProperty(stream, "targetNodeId", typedCommand.targetNodeId.value());
        stream << ',';
        writeIdProperty(stream, "targetEdgeId", typedCommand.targetEdgeId.value());
        stream << ",\"payload\":" << timeline::serializeCanonicalEffectPayload(typedCommand.payload);
        stream << ',';
        foundation::writeJsonStringProperty(stream, "sourcePort", typedCommand.sourcePort.value);
        stream << ',';
        foundation::writeJsonStringProperty(stream, "targetPort", typedCommand.targetPort.value);
        stream << ",\"order\":" << typedCommand.order;
      } else if constexpr (std::is_same_v<Command, DeleteEffectCommand>) {
        writeIdProperty(stream, "nodeId", typedCommand.nodeId.value());
      } else if constexpr (std::is_same_v<Command, ConnectPortsCommand>) {
        writeIdProperty(stream, "edgeId", typedCommand.edgeId.value());
        stream << ',';
        writeIdProperty(stream, "sourceNodeId", typedCommand.sourceNodeId.value());
        stream << ',';
        foundation::writeJsonStringProperty(stream, "sourcePort", typedCommand.sourcePort.value);
        stream << ',';
        writeIdProperty(stream, "targetNodeId", typedCommand.targetNodeId.value());
        stream << ',';
        foundation::writeJsonStringProperty(stream, "targetPort", typedCommand.targetPort.value);
        stream << ",\"order\":" << typedCommand.order;
      } else if constexpr (std::is_same_v<Command, DisconnectPortsCommand>) {
        writeIdProperty(stream, "edgeId", typedCommand.edgeId.value());
      } else if constexpr (std::is_same_v<Command, UpdateEffectParamValueCommand>) {
        writeIdProperty(stream, "effectNodeId", typedCommand.effectNodeId.value());
        stream << ',';
        foundation::writeJsonStringProperty(stream, "paramName", typedCommand.paramName);
        stream << ",\"value\":" << timeline::serializeCanonicalParamValue(typedCommand.value);
      } else if constexpr (std::is_same_v<Command, UpsertEffectParamKeyframeCommand>) {
        writeIdProperty(stream, "effectNodeId", typedCommand.effectNodeId.value());
        stream << ',';
        foundation::writeJsonStringProperty(stream, "paramName", typedCommand.paramName);
        stream << ",\"keyframe\":" << timeline::serializeCanonicalParamKeyframe(typedCommand.keyframe);
      } else if constexpr (std::is_same_v<Command, DeleteEffectParamKeyframeCommand>) {
        writeIdProperty(stream, "effectNodeId", typedCommand.effectNodeId.value());
        stream << ',';
        foundation::writeJsonStringProperty(stream, "paramName", typedCommand.paramName);
        stream << ',';
        writeIdProperty(stream, "keyframeId", typedCommand.keyframeId.value());
      } else if constexpr (std::is_same_v<Command, CreateNoteCommand>) {
        writeNoteCommandPayload(stream, typedCommand.nodeId, typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, UpdateNoteCommand>) {
        writeNoteCommandPayload(stream, typedCommand.nodeId, typedCommand.payload);
      } else if constexpr (std::is_same_v<Command, RestoreSnapshotCommand>) {
        writeIdProperty(stream, "snapshotId", typedCommand.snapshotId.value());
        stream << ",\"snapshot\":" << serializeCanonicalProjectSnapshot(typedCommand.snapshot);
      }

      stream << '}';
      return stream.str();
    },
    command
  );
}

std::string serializeCanonicalEventPayload(const ProjectEvent& event) {
  return std::visit(
    [](const auto& typedEvent) -> std::string {
      using Event = std::decay_t<decltype(typedEvent)>;
      std::ostringstream stream;
      stream << '{';

      if constexpr (std::is_same_v<Event, ProjectCommandAppliedEvent>) {
        writeIdProperty(stream, "commandId", typedEvent.commandId.value());
        stream << ',';
        writeIdProperty(stream, "beforeRevision", typedEvent.beforeRevision.value());
        stream << ',';
        writeIdProperty(stream, "afterRevision", typedEvent.afterRevision.value());
      } else if constexpr (std::is_same_v<Event, ProjectChangedEvent>) {
        writeIdProperty(stream, "projectId", typedEvent.projectId.value());
        stream << ',';
        writeIdProperty(stream, "beforeRevision", typedEvent.beforeRevision.value());
        stream << ',';
        writeIdProperty(stream, "afterRevision", typedEvent.afterRevision.value());
      }

      stream << '}';
      return stream.str();
    },
    event
  );
}

foundation::Hash256 hashProjectSnapshot(const ProjectSnapshot& snapshot) {
  return foundation::stableHash(serializeCanonicalProjectSnapshot(snapshot));
}

} // namespace grapple::project
