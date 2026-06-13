#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/graph/GraphDocument.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <string>
#include <variant>
#include <vector>

namespace grapple::project {

struct GetProjectSnapshotQuery {};
struct GetGraphQuery {};
struct InspectCompositionsQuery {};

struct CompositionClipSummary {
  foundation::NodeId nodeId;
  foundation::NodeId trackNodeId;
  foundation::AssetId assetId;
  timeline::ClipKind kind = timeline::ClipKind::Video;
  foundation::TimeRange timelineRange;
  bool enabled = true;
};

struct CompositionTrackSummary {
  foundation::NodeId nodeId;
  std::string name;
  bool enabled = true;
  std::vector<CompositionClipSummary> clips;
};

struct CompositionCameraSummary {
  foundation::NodeId nodeId;
  std::string name;
  bool enabled = true;
};

struct CompositionEffectSummary {
  foundation::NodeId nodeId;
  foundation::NodeId targetNodeId;
  std::string displayName;
  bool enabled = true;
};

struct CompositionSummary {
  foundation::NodeId nodeId;
  std::string name;
  bool enabled = true;
  std::vector<CompositionTrackSummary> tracks;
  std::vector<CompositionCameraSummary> cameras;
  std::vector<CompositionEffectSummary> effects;
};

using ProjectQuery = std::variant<
  GetProjectSnapshotQuery,
  GetGraphQuery,
  InspectCompositionsQuery
>;

struct ProjectSnapshotResult {
  ProjectSnapshot snapshot;
};

struct GraphResult {
  graph::GraphDocument graph;
};

struct CompositionInspectResult {
  foundation::RevisionId revision;
  std::vector<CompositionSummary> compositions;
};

using ProjectQueryResult = std::variant<
  ProjectSnapshotResult,
  GraphResult,
  CompositionInspectResult
>;

class IProjectQueryService {
public:
  virtual ~IProjectQueryService() = default;

  virtual foundation::Result<ProjectQueryResult> query(const ProjectQuery& query) const = 0;
};

foundation::Result<CompositionInspectResult> inspectCompositions(const ProjectSnapshot& snapshot);

} // namespace grapple::project
