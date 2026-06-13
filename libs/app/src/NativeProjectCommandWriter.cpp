#include <grapple/app/NativeProjectCommandWriter.hpp>

#include <grapple/history/SnapshotRecord.hpp>

#include <chrono>
#include <cctype>
#include <utility>

namespace grapple::app {

NativeProjectCommandWriter::NativeProjectCommandWriter(NativeProjectSession& session)
  : session_{session} {
  commandSequence_ = static_cast<std::int64_t>(session_.packageState().commandLog.records().size()) + 1;
  snapshotSequence_ = static_cast<std::int64_t>(session_.packageState().snapshots.records().size()) + 1;

  const auto snapshot = session_.snapshot();
  if (snapshot) {
    assetSequence_ = static_cast<std::int64_t>(snapshot.value().assets.assets().size()) + 1;
    nodeSequence_ = static_cast<std::int64_t>(snapshot.value().graph.nodes().size()) + 1;
    edgeSequence_ = static_cast<std::int64_t>(snapshot.value().graph.edges().size()) + 1;
  }
}

foundation::AssetId NativeProjectCommandWriter::nextAssetId(const std::string& stem) {
  return foundation::AssetId{"asset_" + sanitizeStem(stem) + "_" + std::to_string(assetSequence_++)};
}

foundation::NodeId NativeProjectCommandWriter::nextNodeId(const std::string& stem) {
  return foundation::NodeId{"node_" + sanitizeStem(stem) + "_" + std::to_string(nodeSequence_++)};
}

foundation::EdgeId NativeProjectCommandWriter::nextEdgeId(const std::string& stem) {
  return foundation::EdgeId{"edge_" + sanitizeStem(stem) + "_" + std::to_string(edgeSequence_++)};
}

foundation::SnapshotId NativeProjectCommandWriter::nextSnapshotId(const std::string& stem) {
  return foundation::SnapshotId{"snap_" + sanitizeStem(stem) + "_" + std::to_string(snapshotSequence_++)};
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
    return "id";
  }
  return sanitized;
}

} // namespace grapple::app
