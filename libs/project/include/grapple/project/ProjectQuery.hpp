#pragma once

#include <grapple/asset/AssetCatalog.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/graph/GraphDocument.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/timeline/Payloads.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace grapple::project {

struct GetProjectSnapshotQuery {};
struct GetGraphQuery {};
struct GetAssetCatalogQuery {};
struct InspectCompositionsQuery {};
struct ListNotesQuery {};
struct InspectEffectGraphsQuery {};
struct InspectRenderPlanQuery {};
struct InspectRuntimeDiagnosticsQuery {};

struct CompositionClipSummary {
  foundation::NodeId nodeId;
  foundation::NodeId trackNodeId;
  foundation::AssetId assetId;
  timeline::ClipKind kind = timeline::ClipKind::Video;
  foundation::TimeRange timelineRange;
  foundation::TimeRange sourceRange;
  double playbackRate = 1.0;
  timeline::Transform2D transform;
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
  timeline::Transform2D transform;
  timeline::CameraLens lens;
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

struct NoteSummary {
  foundation::NodeId nodeId;
  std::string title;
  std::string markdown;
  bool enabled = true;
};

struct EffectGraphPortSummary {
  std::string name;
};

struct EffectGraphParamKeyframeSummary {
  foundation::KeyframeId keyframeId;
  foundation::TimeSeconds time;
  timeline::ParamValue value;
};

struct EffectGraphParamSummary {
  std::string name;
  timeline::ParamValue value;
  std::string label;
  std::optional<timeline::Param::NumericControl> numeric;
  std::vector<EffectGraphParamKeyframeSummary> keyframes;
};

struct EffectGraphNodeSummary {
  foundation::NodeId nodeId;
  std::string displayName;
  timeline::EffectImplementationKind implementationKind = timeline::EffectImplementationKind::Python;
  std::string entrypoint;
  timeline::EffectSourceKind sourceKind = timeline::EffectSourceKind::InlineSource;
  std::string language;
  std::string inlineSource;
  std::optional<foundation::AssetId> sourceAssetId;
  foundation::Hash256 sourceHash;
  std::vector<EffectGraphPortSummary> inputPorts;
  std::vector<EffectGraphPortSummary> outputPorts;
  std::vector<EffectGraphParamSummary> params;
  foundation::TimeRange activeRange;
  bool enabled = true;
};

struct EffectGraphEdgeSummary {
  foundation::EdgeId edgeId;
  foundation::NodeId sourceNodeId;
  graph::PortName sourcePort;
  foundation::NodeId targetNodeId;
  graph::PortName targetPort;
  std::int64_t order = 0;
  bool enabled = true;
};

struct EffectGraphSummary {
  foundation::GraphId graphId;
  foundation::NodeId targetNodeId;
  std::vector<EffectGraphNodeSummary> nodes;
  std::vector<EffectGraphEdgeSummary> edges;
};

struct EffectGraphsInspectResult {
  foundation::RevisionId revision;
  std::vector<EffectGraphSummary> effectGraphs;
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
  GetAssetCatalogQuery,
  InspectCompositionsQuery,
  ListNotesQuery,
  InspectEffectGraphsQuery,
  InspectRenderPlanQuery,
  InspectRuntimeDiagnosticsQuery
>;

struct ProjectSnapshotResult {
  ProjectSnapshot snapshot;
};

struct GraphResult {
  foundation::RevisionId revision;
  graph::GraphDocument graph;
};

struct AssetCatalogResult {
  foundation::RevisionId revision;
  asset::AssetCatalog assets;
};

struct CompositionInspectResult {
  foundation::RevisionId revision;
  std::vector<CompositionSummary> compositions;
};

struct NotesResult {
  foundation::RevisionId revision;
  std::vector<NoteSummary> notes;
};

using ProjectQueryResult = std::variant<
  ProjectSnapshotResult,
  GraphResult,
  AssetCatalogResult,
  CompositionInspectResult,
  NotesResult,
  EffectGraphsInspectResult,
  RenderPlanInspectResult,
  RuntimeInspectDiagnosticsResult
>;

class IProjectQueryService {
public:
  virtual ~IProjectQueryService() = default;

  virtual foundation::Result<ProjectQueryResult> query(const ProjectQuery& query) const = 0;
};

foundation::Result<CompositionInspectResult> inspectCompositions(const ProjectSnapshot& snapshot);
foundation::Result<NotesResult> listNotes(const ProjectSnapshot& snapshot);
foundation::Result<EffectGraphsInspectResult> inspectEffectGraphs(const ProjectSnapshot& snapshot);

} // namespace grapple::project
