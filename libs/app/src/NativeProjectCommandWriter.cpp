#include <grapple/app/NativeProjectCommandWriter.hpp>

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
    nodeSequence_ = static_cast<std::int64_t>(snapshot.value().graph.nodes().size()) + 1;
    edgeSequence_ = static_cast<std::int64_t>(snapshot.value().graph.edges().size()) + 1;
  }
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

  return session_.applyAndCommit(
    project::ProjectCommandEnvelope{
      nextCommandId(),
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
