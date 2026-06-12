#pragma once

#include <grapple/asset/AssetCatalog.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/graph/GraphDocument.hpp>
#include <grapple/project/ProjectDocument.hpp>
#include <grapple/project/ProjectInfo.hpp>
#include <grapple/project/ProjectSettings.hpp>

#include <cstdint>

namespace grapple::project {

struct ProjectSnapshot {
  ProjectInfo info;
  foundation::RevisionId revision;
  std::int64_t revisionNumber = 0;
  ProjectSettings settings;
  asset::AssetCatalog assets;
  graph::GraphDocument graph;
  foundation::Hash256 canonicalHash;
};

[[nodiscard]] ProjectSnapshot makeProjectSnapshot(const ProjectDocument& document);

} // namespace grapple::project
