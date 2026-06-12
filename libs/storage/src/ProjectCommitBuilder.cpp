#include <grapple/storage/ProjectCommitBuilder.hpp>

#include <grapple/project/ProjectCommandNames.hpp>
#include <grapple/project/ProjectEventNames.hpp>
#include <grapple/project/ProjectSerializer.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace grapple::storage {

namespace {

foundation::EventId makeEventId(foundation::CommandId commandId, std::size_t eventIndex) {
  return foundation::EventId{
    "event_" + commandId.value() + "_" + std::to_string(eventIndex)
  };
}

} // namespace

AtomicProjectCommit makeAtomicProjectCommit(
  project::ProjectSnapshot projectSnapshot,
  const project::ProjectCommandEnvelope& command,
  const project::ProjectCommandResult& result,
  ProjectCommitRecordOptions options
) {
  std::vector<history::EventRecord> events;
  events.reserve(result.events.size());
  for (std::size_t index = 0; index < result.events.size(); ++index) {
    const project::ProjectEvent& event = result.events[index];
    events.push_back(history::EventRecord{
      makeEventId(command.id, index),
      command.projectId,
      result.afterRevision,
      std::string{project::serializedEventName(project::eventKind(event))},
      project::serializeCanonicalEventPayload(event),
      options.createdAt
    });
  }

  std::optional<history::SnapshotRecord> snapshot;
  if (options.snapshot.has_value()) {
    snapshot = history::SnapshotRecord{
      options.snapshot->id,
      projectSnapshot.info.id,
      projectSnapshot.revision,
      projectSnapshot.canonicalHash,
      options.snapshot->documentPath,
      options.snapshot->label,
      options.createdAt
    };
  }

  return AtomicProjectCommit{
    std::move(projectSnapshot),
    history::CommandRecord{
      command.id,
      command.projectId,
      result.beforeRevision,
      result.afterRevision,
      std::string{project::serializedCommandName(project::commandKind(command.payload))},
      project::serializeCanonicalCommandPayload(command.payload),
      std::string{project::serializedCommandSourceKind(command.source.kind)},
      command.source.runId,
      command.source.actorName,
      options.createdAt
    },
    std::move(events),
    std::move(snapshot)
  };
}

} // namespace grapple::storage
