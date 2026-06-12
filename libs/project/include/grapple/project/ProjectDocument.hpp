#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/graph/GraphDocument.hpp>

#include <cstdint>
#include <string>

namespace grapple::project {

struct ProjectInfo {
  foundation::ProjectId id;
  std::string name;
};

struct ProjectDocument {
  ProjectInfo info;
  foundation::RevisionId revision;
  std::int64_t revisionNumber = 0;
  graph::GraphDocument graph;
};

struct ProjectSnapshot {
  ProjectDocument document;
};

} // namespace grapple::project

