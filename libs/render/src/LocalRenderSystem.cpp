#include <grapple/render/LocalRenderSystem.hpp>

#include <mutex>

namespace grapple::render {

LocalRenderSystem::LocalRenderSystem(LocalRenderCore& core)
  : core_{core},
    preview_{core},
    finalRender_{core} {}

foundation::Result<void> LocalRenderSystem::loadPlan(const projection::RenderPlan& plan) {
  std::lock_guard lock{mutex_};
  return core_.loadPlan(plan);
}

foundation::Result<void> LocalRenderSystem::seek(foundation::TimeSeconds time) {
  std::lock_guard lock{mutex_};
  return preview_.seek(time);
}

foundation::Result<void> LocalRenderSystem::play() {
  std::lock_guard lock{mutex_};
  return preview_.play();
}

foundation::Result<void> LocalRenderSystem::pause() {
  std::lock_guard lock{mutex_};
  return preview_.pause();
}

foundation::Result<PlaybackFrameResult> LocalRenderSystem::renderPlaybackFrame(
  const PlaybackFrameRequest& request
) const {
  std::lock_guard lock{mutex_};
  return preview_.renderFrame(RenderFrameRequest{
    request.time,
    request.quality
  });
}

foundation::Result<ExportResult> LocalRenderSystem::exportRange(const ExportRequest& request) {
  std::lock_guard lock{mutex_};
  return finalRender_.render(FinalRenderRequest{
    request.settings,
    request.sink
  });
}

foundation::Result<ExportResult> LocalRenderSystem::exportPlanRange(const ExportPlanRequest& request) {
  std::lock_guard lock{mutex_};
  auto loadResult = core_.loadPlan(request.plan);
  if (!loadResult) {
    return loadResult.error();
  }
  return finalRender_.render(FinalRenderRequest{
    request.settings,
    request.sink
  });
}

LocalRenderSystemState LocalRenderSystem::state() const {
  std::lock_guard lock{mutex_};
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
