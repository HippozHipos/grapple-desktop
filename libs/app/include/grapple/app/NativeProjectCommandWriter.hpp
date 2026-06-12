#pragma once

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace grapple::app {

class NativeProjectCommandWriter {
public:
  explicit NativeProjectCommandWriter(NativeProjectSession& session);

  [[nodiscard]] foundation::AssetId nextAssetId(const std::string& stem);
  [[nodiscard]] foundation::NodeId nextNodeId(const std::string& stem);
  [[nodiscard]] foundation::EdgeId nextEdgeId(const std::string& stem);
  [[nodiscard]] foundation::SnapshotId nextSnapshotId(const std::string& stem);

  foundation::Result<storage::ProjectPackageSessionResult> apply(
    project::ProjectCommand command,
    project::CommandSource source,
    std::optional<storage::SnapshotCommitRecord> snapshot = std::nullopt
  );

private:
  [[nodiscard]] foundation::CommandId nextCommandId();
  [[nodiscard]] static std::string sanitizeStem(const std::string& stem);

  NativeProjectSession& session_;
  std::int64_t commandSequence_ = 1;
  std::int64_t assetSequence_ = 1;
  std::int64_t nodeSequence_ = 1;
  std::int64_t edgeSequence_ = 1;
  std::int64_t snapshotSequence_ = 1;
};

} // namespace grapple::app
