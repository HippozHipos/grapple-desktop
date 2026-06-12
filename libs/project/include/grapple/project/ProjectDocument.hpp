#pragma once

#include <grapple/asset/AssetCatalog.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/graph/GraphDocument.hpp>
#include <grapple/project/ProjectInfo.hpp>
#include <grapple/project/ProjectSettings.hpp>

#include <cstdint>

namespace grapple::project {

struct ProjectDocument {
  ProjectInfo info;
  foundation::RevisionId revision;
  std::int64_t revisionNumber = 0;
  ProjectSettings settings;
  asset::AssetCatalog assets;
  graph::GraphDocument graph;
};

} // namespace grapple::project
