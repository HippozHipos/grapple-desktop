#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/foundation/Transform.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace grapple::app {

struct AppProjectSummary {
  foundation::ProjectId projectId;
  std::string name;
  foundation::RevisionId revision;
  std::int64_t revisionNumber = 0;
  foundation::Hash256 canonicalHash;
};

struct AppStewardEditRow {
  foundation::CommandId commandId;
  foundation::RevisionId revision;
  std::string intent;
};

struct AppStewardView {
  std::vector<AppStewardEditRow> edits;
};

struct AppCompositionRow {
  foundation::NodeId sourceNodeId;
  std::string name;
};

struct AppAssetRow {
  foundation::AssetId assetId;
  std::string name;
  std::string mediaType;
  foundation::FilePath sourcePath;
  std::optional<foundation::TimeSeconds> duration;
  std::optional<foundation::Resolution> dimensions;
};

struct AppAssetSummary {
  std::size_t count = 0;
  std::vector<AppAssetRow> rows;
};

struct AppLayerRow {
  foundation::NodeId sourceNodeId;
  std::string name;
  std::size_t clipCount = 0;
};

struct AppClipRow {
  foundation::NodeId sourceNodeId;
  foundation::NodeId trackNodeId;
  foundation::AssetId assetId;
  foundation::TimeRange timelineRange;
  foundation::Transform2D transform;
};

struct AppCameraRow {
  foundation::NodeId sourceNodeId;
  std::string name;
  foundation::Transform2D transform;
};

struct AppEffectParamRow {
  std::string name;
  std::string label;
  std::string value;
  std::optional<double> numericMin;
  std::optional<double> numericMax;
  std::optional<double> numericStep;
};

struct AppEffectRow {
  foundation::GraphId graphId;
  foundation::NodeId sourceNodeId;
  foundation::NodeId targetNodeId;
  std::string displayName;
  std::string implementationKind;
  std::string entrypoint;
  foundation::TimeRange activeRange;
  std::vector<AppEffectParamRow> params;
};

struct AppEffectGraphRow {
  foundation::GraphId graphId;
  foundation::NodeId targetNodeId;
  std::size_t nodeCount = 0;
  std::size_t edgeCount = 0;
  std::vector<AppEffectRow> effects;
};

struct AppTimelineView {
  foundation::TimeSeconds duration;
  std::vector<AppCompositionRow> compositions;
  std::vector<AppLayerRow> layers;
  std::vector<AppClipRow> clips;
  std::vector<AppCameraRow> cameras;
  std::vector<AppEffectGraphRow> effectGraphs;
};

struct AppViewModel {
  AppProjectSummary project;
  AppStewardView steward;
  AppAssetSummary assets;
  AppTimelineView timeline;
};

} // namespace grapple::app
