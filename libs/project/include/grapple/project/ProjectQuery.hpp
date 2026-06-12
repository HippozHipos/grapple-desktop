#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/graph/GraphDocument.hpp>
#include <grapple/project/ProjectDocument.hpp>

#include <variant>

namespace grapple::project {

struct GetProjectSnapshotQuery {};
struct GetGraphQuery {};

using ProjectQuery = std::variant<
  GetProjectSnapshotQuery,
  GetGraphQuery
>;

struct ProjectSnapshotResult {
  ProjectSnapshot snapshot;
};

struct GraphResult {
  graph::GraphDocument graph;
};

using ProjectQueryResult = std::variant<
  ProjectSnapshotResult,
  GraphResult
>;

class IProjectQueryService {
public:
  virtual ~IProjectQueryService() = default;

  virtual foundation::Result<ProjectQueryResult> query(const ProjectQuery& query) const = 0;
};

} // namespace grapple::project
