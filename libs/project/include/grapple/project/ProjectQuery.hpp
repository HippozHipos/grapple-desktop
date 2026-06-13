#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/graph/GraphDocument.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace grapple::project {

struct GetProjectSnapshotQuery {};
struct GetGraphQuery {};
struct InspectCompositionsQuery {};
struct InspectRenderPlanQuery {};
struct InspectRuntimeDiagnosticsQuery {};

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
  timeline::TrackKind kind = timeline::TrackKind::Visual;
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

struct RenderPlanLayerSummary {
  foundation::NodeId nodeId;
  std::string name;
};

struct RenderPlanClipSummary {
  foundation::NodeId nodeId;
  foundation::NodeId trackNodeId;
  foundation::AssetId assetId;
  timeline::ClipKind kind = timeline::ClipKind::Video;
  foundation::TimeRange timelineRange;
};

struct RenderPlanCameraSummary {
  foundation::NodeId nodeId;
  std::string name;
};

struct RenderPlanEffectGraphSummary {
  foundation::GraphId graphId;
  foundation::NodeId targetNodeId;
  std::size_t nodeCount = 0;
  std::size_t edgeCount = 0;
};

struct RenderPlanInspectResult {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  foundation::TimeSeconds duration;
  std::size_t assetCount = 0;
  std::vector<RenderPlanLayerSummary> layers;
  std::vector<RenderPlanLayerSummary> audioTracks;
  std::vector<RenderPlanClipSummary> clips;
  std::vector<RenderPlanClipSummary> audioClips;
  std::vector<RenderPlanCameraSummary> cameras;
  std::vector<RenderPlanEffectGraphSummary> effectGraphs;
  std::size_t diagnosticCount = 0;
};

enum class RuntimeDiagnosticSeveritySummary {
  Info,
  Warning,
  Error
};

struct RuntimeDiagnosticSummary {
  std::string code;
  RuntimeDiagnosticSeveritySummary severity = RuntimeDiagnosticSeveritySummary::Error;
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  std::optional<foundation::NodeId> nodeId;
  std::string message;
};

struct RuntimeInspectDiagnosticsResult {
  foundation::RevisionId revision;
  std::vector<RuntimeDiagnosticSummary> diagnostics;
};

using ProjectQuery = std::variant<
  GetProjectSnapshotQuery,
  GetGraphQuery,
  InspectCompositionsQuery,
  InspectRenderPlanQuery,
  InspectRuntimeDiagnosticsQuery
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
  CompositionInspectResult,
  RenderPlanInspectResult,
  RuntimeInspectDiagnosticsResult
>;

class IProjectQueryService {
public:
  virtual ~IProjectQueryService() = default;

  virtual foundation::Result<ProjectQueryResult> query(const ProjectQuery& query) const = 0;
};

foundation::Result<CompositionInspectResult> inspectCompositions(const ProjectSnapshot& snapshot);

} // namespace grapple::project
