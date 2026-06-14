#include <grapple/app/NativeProjectCommandWriter.hpp>

#include <grapple/graph/GraphNode.hpp>
#include <grapple/history/CommandRecord.hpp>
#include <grapple/history/SnapshotRecord.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

bool keyframeIdExists(const project::ProjectSnapshot& snapshot, const foundation::KeyframeId& keyframeId) {
  for (const graph::GraphNode& node : snapshot.graph.nodes()) {
    const auto* effect = std::get_if<timeline::EffectPayload>(&node.payload);
    if (effect == nullptr) {
      continue;
    }
    for (const timeline::Param& param : effect->params.values) {
      const auto keyframe = std::find_if(param.keyframes.begin(), param.keyframes.end(), [&](const timeline::Param::Keyframe& current) {
        return current.id == keyframeId;
      });
      if (keyframe != param.keyframes.end()) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

NativeProjectCommandWriter::NativeProjectCommandWriter(NativeProjectSession& session)
  : session_{session} {
  commandSequence_ = static_cast<std::int64_t>(session_.packageState().commandLog.records().size()) + 1;
  snapshotSequence_ = static_cast<std::int64_t>(session_.packageState().snapshots.records().size()) + 1;

  const auto snapshot = session_.snapshot();
  if (snapshot) {
    assetSequence_ = static_cast<std::int64_t>(snapshot.value().assets.assets().size()) + 1;
    nodeSequence_ = static_cast<std::int64_t>(snapshot.value().graph.nodes().size()) + 1;
    edgeSequence_ = static_cast<std::int64_t>(snapshot.value().graph.edges().size()) + 1;
    std::size_t keyframeCount = 0;
    for (const graph::GraphNode& node : snapshot.value().graph.nodes()) {
      const auto* effect = std::get_if<timeline::EffectPayload>(&node.payload);
      if (effect == nullptr) {
        continue;
      }
      for (const timeline::Param& param : effect->params.values) {
        keyframeCount += param.keyframes.size();
      }
    }
    keyframeSequence_ = static_cast<std::int64_t>(keyframeCount) + 1;
  }
}

foundation::AssetId NativeProjectCommandWriter::nextAssetId(const std::string& stem) {
  const std::string sanitized = sanitizeStem(stem);
  return foundation::AssetId{"asset_" + sanitized + "_" + std::to_string(assetSequence_++)};
}

foundation::NodeId NativeProjectCommandWriter::nextNodeId(const std::string& stem) {
  const std::string sanitized = sanitizeStem(stem);
  return foundation::NodeId{"node_" + sanitized + "_" + std::to_string(nodeSequence_++)};
}

foundation::EdgeId NativeProjectCommandWriter::nextEdgeId(const std::string& stem) {
  const std::string sanitized = sanitizeStem(stem);
  return foundation::EdgeId{"edge_" + sanitized + "_" + std::to_string(edgeSequence_++)};
}

foundation::KeyframeId NativeProjectCommandWriter::nextKeyframeId(const std::string& stem) {
  const std::string sanitized = sanitizeStem(stem);
  const auto snapshot = session_.snapshot();
  if (!snapshot) {
    throw std::runtime_error{"Cannot allocate keyframe id without a project snapshot."};
  }

  while (true) {
    foundation::KeyframeId candidate{"key_" + sanitized + "_" + std::to_string(keyframeSequence_++)};
    if (!keyframeIdExists(snapshot.value(), candidate)) {
      return candidate;
    }
  }
}

foundation::SnapshotId NativeProjectCommandWriter::nextSnapshotId(const std::string& stem) {
  const std::string sanitized = sanitizeStem(stem);
  while (true) {
    foundation::SnapshotId candidate{"snap_" + sanitized + "_" + std::to_string(snapshotSequence_++)};
    const auto exists = std::any_of(
      session_.packageState().snapshots.records().begin(),
      session_.packageState().snapshots.records().end(),
      [&](const history::SnapshotRecord& record) {
        return record.id == candidate;
      }
    );
    if (!exists) {
      return candidate;
    }
  }
}

foundation::Result<storage::ProjectPackageSessionResult> NativeProjectCommandWriter::apply(
  project::ProjectCommand command,
  project::CommandSource source,
  std::optional<storage::SnapshotCommitRecord> snapshot
) {
  auto currentSnapshot = session_.snapshot();
  if (!currentSnapshot) {
    return currentSnapshot.error();
  }

  foundation::CommandId commandId = nextCommandId();
  if (!snapshot.has_value()) {
    foundation::SnapshotId snapshotId = nextSnapshotId(commandId.value());
    snapshot = storage::SnapshotCommitRecord{
      snapshotId,
      foundation::FilePath{"snapshots/" + snapshotId.value() + ".json"},
      std::nullopt
    };
  }

  return session_.applyAndCommit(
    project::ProjectCommandEnvelope{
      std::move(commandId),
      currentSnapshot.value().info.id,
      currentSnapshot.value().revision,
      std::move(source),
      std::move(command)
    },
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      std::move(snapshot)
    }
  );
}

foundation::Result<project::ProjectCommandResult> NativeProjectCommandWriter::apply(
  const project::ProjectCommandEnvelope& command
) {
  foundation::SnapshotId snapshotId = nextSnapshotId(command.id.value());
  auto committed = session_.applyAndCommit(
    command,
    storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      storage::SnapshotCommitRecord{
        snapshotId,
        foundation::FilePath{"snapshots/" + snapshotId.value() + ".json"},
        std::nullopt
      }
    }
  );
  if (!committed) {
    return committed.error();
  }
  return committed.value().commandResult;
}

foundation::Result<storage::ProjectPackageSessionResult> NativeProjectCommandWriter::restoreCommittedRevision(
  foundation::RevisionId revision,
  project::CommandSource source,
  std::optional<std::string> snapshotLabel
) {
  const project::ProjectSnapshot* snapshot = session_.findCommittedSnapshot(revision);
  if (snapshot == nullptr) {
    return foundation::Error{
      "app.committed_snapshot_missing",
      "Committed snapshot document not found for revision " + revision.value() + "."
    };
  }

  const history::SnapshotRecord* snapshotRecord = session_.packageState().snapshots.findByRevision(revision);
  if (snapshotRecord == nullptr) {
    return foundation::Error{
      "app.committed_snapshot_record_missing",
      "Committed snapshot record not found for revision " + revision.value() + "."
    };
  }

  foundation::SnapshotId restoredSnapshotId = nextSnapshotId("restore_" + revision.value());
  return apply(
    project::RestoreSnapshotCommand{
      snapshotRecord->id,
      *snapshot
    },
    std::move(source),
    storage::SnapshotCommitRecord{
      restoredSnapshotId,
      foundation::FilePath{"snapshots/" + restoredSnapshotId.value() + ".json"},
      std::move(snapshotLabel)
    }
  );
}

foundation::Result<storage::ProjectPackageSessionResult> NativeProjectCommandWriter::undoLastCommittedCommand(
  project::CommandSource source,
  std::optional<std::string> snapshotLabel
) {
  const storage::ProjectPackageState& state = session_.packageState();
  if (!state.head.has_value() || !state.head->lastCommandId.has_value()) {
    return foundation::Error{
      "app.undo_command_missing",
      "Undo requires a committed command at the current project head."
    };
  }

  const std::vector<history::CommandRecord>& records = state.commandLog.records();
  const auto command = std::find_if(records.rbegin(), records.rend(), [&](const history::CommandRecord& record) {
    return record.id == *state.head->lastCommandId;
  });
  if (command == records.rend()) {
    return foundation::Error{
      "app.undo_head_command_missing",
      "Undo could not find the command record for the current project head."
    };
  }
  if (command->afterRevision != state.head->currentRevision) {
    return foundation::Error{
      "app.undo_head_revision_mismatch",
      "Undo command record does not match the current project head revision."
    };
  }

  return restoreCommittedRevision(
    command->beforeRevision,
    std::move(source),
    std::move(snapshotLabel)
  );
}

foundation::Result<storage::ProjectPackageSessionResult> NativeProjectCommandWriter::redoLastUndoneCommand(
  project::CommandSource source,
  std::optional<std::string> snapshotLabel
) {
  const storage::ProjectPackageState& state = session_.packageState();
  if (!state.head.has_value() || !state.head->lastCommandId.has_value()) {
    return foundation::Error{
      "app.redo_command_missing",
      "Redo requires a committed restore command at the current project head."
    };
  }

  const std::vector<history::CommandRecord>& records = state.commandLog.records();
  const auto headCommand = std::find_if(records.rbegin(), records.rend(), [&](const history::CommandRecord& record) {
    return record.id == *state.head->lastCommandId;
  });
  if (headCommand == records.rend()) {
    return foundation::Error{
      "app.redo_head_command_missing",
      "Redo could not find the command record for the current project head."
    };
  }
  if (headCommand->afterRevision != state.head->currentRevision) {
    return foundation::Error{
      "app.redo_head_revision_mismatch",
      "Redo command record does not match the current project head revision."
    };
  }
  if (headCommand->serializedName != "project.restore_snapshot") {
    return foundation::Error{
      "app.redo_requires_restore_head",
      "Redo requires the current project head command to be a restore command."
    };
  }

  auto restoredCommand = project::deserializeCanonicalCommandPayload(
    headCommand->serializedName,
    headCommand->serializedPayload
  );
  if (!restoredCommand) {
    return restoredCommand.error();
  }
  const auto* restoredSnapshot = std::get_if<project::RestoreSnapshotCommand>(&restoredCommand.value());
  if (restoredSnapshot == nullptr) {
    return foundation::Error{
      "app.redo_restore_payload_invalid",
      "Redo restore command payload must contain a restored snapshot."
    };
  }

  const auto redoneCommand = std::find_if(records.rbegin(), records.rend(), [&](const history::CommandRecord& record) {
    return record.beforeRevision == restoredSnapshot->snapshot.revision &&
      record.afterRevision == headCommand->beforeRevision &&
      record.serializedName != "project.restore_snapshot";
  });
  if (redoneCommand == records.rend()) {
    return foundation::Error{
      "app.redo_intent_missing",
      "Redo could not find the stored command intent reversed by the current restore command."
    };
  }

  auto intent = project::deserializeCanonicalCommandPayload(
    redoneCommand->serializedName,
    redoneCommand->serializedPayload
  );
  if (!intent) {
    return intent.error();
  }

  foundation::SnapshotId snapshotId = nextSnapshotId("redo_" + redoneCommand->afterRevision.value());
  return apply(
    std::move(intent.value()),
    std::move(source),
    storage::SnapshotCommitRecord{
      snapshotId,
      foundation::FilePath{"snapshots/" + snapshotId.value() + ".json"},
      std::move(snapshotLabel)
    }
  );
}

foundation::CommandId NativeProjectCommandWriter::nextCommandId() {
  return foundation::CommandId{"cmd_app_" + std::to_string(commandSequence_++)};
}

std::string NativeProjectCommandWriter::sanitizeStem(const std::string& stem) {
  std::string sanitized;
  sanitized.reserve(stem.size());
  bool lastWasUnderscore = false;
  for (const unsigned char character : stem) {
    if (std::isalnum(character)) {
      sanitized.push_back(static_cast<char>(std::tolower(character)));
      lastWasUnderscore = false;
    } else if (!lastWasUnderscore) {
      sanitized.push_back('_');
      lastWasUnderscore = true;
    }
  }

  while (!sanitized.empty() && sanitized.back() == '_') {
    sanitized.pop_back();
  }

  if (sanitized.empty()) {
    throw std::invalid_argument{"Project id stem must contain at least one alphanumeric character."};
  }
  return sanitized;
}

} // namespace grapple::app
