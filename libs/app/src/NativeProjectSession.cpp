#include <grapple/app/NativeProjectSession.hpp>

#include <utility>
#include <variant>

namespace grapple::app {

NativeProjectSession::NativeProjectSession(
  foundation::ProjectId projectId,
  std::string projectName,
  storage::ProjectPackage package
) : NativeProjectSession{
      project::createEmptyProject(std::move(projectId), std::move(projectName)),
      std::move(package)
    } {}

NativeProjectSession::NativeProjectSession(project::ProjectDocument document, storage::ProjectPackage package)
  : session_{std::move(document), std::move(package)} {}

foundation::Result<storage::ProjectPackageSessionResult> NativeProjectSession::applyAndCommit(
  const project::ProjectCommandEnvelope& command,
  storage::ProjectCommitRecordOptions options
) {
  return session_.applyAndCommit(command, std::move(options));
}

foundation::Result<project::ProjectSnapshot> NativeProjectSession::snapshot() const {
  return session_.snapshot();
}

foundation::Result<project::ProjectQueryResult> NativeProjectSession::query(const project::ProjectQuery& query) const {
  auto snapshotResult = session_.snapshot();
  if (!snapshotResult) {
    return snapshotResult.error();
  }

  return std::visit(
    [&](const auto& typedQuery) -> foundation::Result<project::ProjectQueryResult> {
      using Query = std::decay_t<decltype(typedQuery)>;
      if constexpr (std::is_same_v<Query, project::GetProjectSnapshotQuery>) {
        return project::ProjectQueryResult{project::ProjectSnapshotResult{snapshotResult.value()}};
      } else if constexpr (std::is_same_v<Query, project::GetGraphQuery>) {
        return project::ProjectQueryResult{project::GraphResult{snapshotResult.value().graph}};
      }
    },
    query
  );
}

foundation::Result<projection::BuildTimelineIRResult> NativeProjectSession::buildTimelineIR() const {
  const projection::ProjectionQueryService projectionQueries{*this};
  return projectionQueries.buildCurrentTimelineIR();
}

foundation::Result<projection::BuildRenderPlanResult> NativeProjectSession::buildRenderPlan() const {
  const projection::ProjectionQueryService projectionQueries{*this};
  return projectionQueries.buildCurrentRenderPlan();
}

const storage::ProjectPackageState& NativeProjectSession::packageState() const noexcept {
  return session_.packageState();
}

} // namespace grapple::app
