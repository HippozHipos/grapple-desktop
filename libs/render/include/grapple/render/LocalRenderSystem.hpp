#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/FinalRenderShell.hpp>
#include <grapple/render/PreviewRenderShell.hpp>

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

using ExportResult = FinalRenderResult;

struct LocalRenderSystemState {
  PreviewRenderShellState preview;
  FinalRenderShellState finalRender;
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
  [[nodiscard]] LocalRenderSystemState state() const noexcept;

private:
  LocalRenderCore& core_;
  PreviewRenderShell preview_;
  FinalRenderShell finalRender_;
};

} // namespace grapple::render
