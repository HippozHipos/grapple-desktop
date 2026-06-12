#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/RenderDiagnostic.hpp>
#include <grapple/render/RenderQuality.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <optional>
#include <string>
#include <vector>

namespace grapple::render {

enum class RenderedMediaKind {
  Video,
  Image
};

struct RenderedMediaFrame {
  foundation::NodeId clipNodeId;
  foundation::NodeId trackNodeId;
  foundation::AssetId assetId;
  RenderedMediaKind kind = RenderedMediaKind::Video;
  foundation::TimeSeconds sourceTime;
};

struct RenderFrame {
  foundation::TimeSeconds time;
  std::string description;
  std::vector<RenderedMediaFrame> mediaFrames;
};

struct RenderFrameRequest {
  foundation::TimeSeconds time;
  RenderQuality quality = RenderQuality::Draft;
};

struct RenderFrameResult {
  RenderFrame frame;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
};

struct RenderRangeRequest {
  foundation::TimeRange range;
  foundation::FrameRate frameRate;
  RenderQuality quality = RenderQuality::Final;
};

struct RenderRangeResult {
  std::size_t framesEvaluated = 0;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
};

struct LocalRenderCoreState {
  bool hasPlan = false;
  std::optional<foundation::RevisionId> revision;
  std::optional<foundation::Hash256> preparedPlanHash;
};

class LocalRenderCore {
public:
  explicit LocalRenderCore(runtime::RuntimeEvaluator& runtime);

  foundation::Result<void> loadPlan(const projection::RenderPlan& plan);
  foundation::Result<RenderFrameResult> renderFrame(const RenderFrameRequest& request) const;
  foundation::Result<RenderRangeResult> renderRange(const RenderRangeRequest& request) const;
  [[nodiscard]] LocalRenderCoreState state() const noexcept;

private:
  runtime::RuntimeEvaluator& runtime_;
  std::optional<runtime::PreparedRuntimePlan> prepared_;
  LocalRenderCoreState state_;
};

} // namespace grapple::render
