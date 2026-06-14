#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/FinalRenderShell.hpp>
#include <grapple/render/PreviewRenderShell.hpp>

#include <mutex>
#include <optional>

namespace grapple::render {

struct PlaybackFrameRequest {
  foundation::TimeSeconds time;
  RenderQuality quality = RenderQuality::Draft;
};

using PlaybackFrameResult = RenderFrameResult;

struct ExportRequest {
  ExportSettings settings;
  IRenderRangeSink* sink = nullptr;
};

struct ExportPlanRequest {
  projection::RenderPlan plan;
  ExportSettings settings;
  IRenderRangeSink* sink = nullptr;
};

using ExportResult = FinalRenderResult;

struct LocalRenderSystemState {
  LocalRenderCoreState core;
  PreviewPlaybackState playback = PreviewPlaybackState::Paused;
  foundation::TimeSeconds playhead;
  std::optional<ExportSettings> lastExportSettings;
  std::optional<foundation::FilePath> lastExportOutputPath;
};

class LocalRenderSystem {
public:
  explicit LocalRenderSystem(LocalRenderCore& core);

  foundation::Result<void> loadPlan(const projection::RenderPlan& plan);
  foundation::Result<void> seek(foundation::TimeSeconds time);
  foundation::Result<void> play();
  foundation::Result<void> pause();
  foundation::Result<PlaybackFrameResult> renderPlaybackFrame(const PlaybackFrameRequest& request) const;
  foundation::Result<ExportResult> exportRange(const ExportRequest& request);
  foundation::Result<ExportResult> exportPlanRange(const ExportPlanRequest& request);
  [[nodiscard]] LocalRenderSystemState state() const;

private:
  LocalRenderCore& core_;
  PreviewRenderShell preview_;
  FinalRenderShell finalRender_;
  mutable std::mutex mutex_;
};

} // namespace grapple::render
