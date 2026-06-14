#include <grapple/render/LocalRenderSystem.hpp>

namespace grapple::render {

LocalRenderSystem::LocalRenderSystem(LocalRenderCore& core)
  : core_{core},
    preview_{core},
    finalRender_{core} {}

foundation::Result<void> LocalRenderSystem::loadPlan(const projection::RenderPlan& plan) {
  return core_.loadPlan(plan);
}

foundation::Result<void> LocalRenderSystem::seek(foundation::TimeSeconds time) {
  return preview_.seek(time);
}

foundation::Result<void> LocalRenderSystem::play() {
  return preview_.play();
}

foundation::Result<void> LocalRenderSystem::pause() {
  return preview_.pause();
}

foundation::Result<PlaybackFrameResult> LocalRenderSystem::renderPlaybackFrame(
  const PlaybackFrameRequest& request
) const {
  return preview_.renderFrame(RenderFrameRequest{
    request.time,
    request.quality
  });
}

foundation::Result<ExportResult> LocalRenderSystem::exportRange(const ExportRequest& request) {
  return finalRender_.render(FinalRenderRequest{
    request.settings,
    request.sink
  });
}

LocalRenderSystemState LocalRenderSystem::state() const noexcept {
  const PreviewRenderShellState previewState = preview_.state();
  const FinalRenderShellState finalRenderState = finalRender_.state();
  return LocalRenderSystemState{
    previewState.core,
    previewState.playback,
    previewState.playhead,
    finalRenderState.lastSettings,
    finalRenderState.lastOutputPath
  };
}

} // namespace grapple::render
