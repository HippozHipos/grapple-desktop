#include <grapple/app/NativePreviewSession.hpp>

namespace grapple::app {

NativePreviewSession::NativePreviewSession(
  NativeProjectSession& project,
  render::LocalRenderSystem& renderSystem
) : project_{project},
    renderSystem_{renderSystem} {}

foundation::Result<NativePreviewRefreshResult> NativePreviewSession::refreshFromProject() {
  auto planResult = project_.buildRenderPlan();
  if (!planResult) {
    return planResult.error();
  }

  auto loadResult = renderSystem_.loadPlan(planResult.value().plan);
  if (!loadResult) {
    return loadResult.error();
  }

  const render::LocalRenderSystemState renderState = renderSystem_.state();
  return NativePreviewRefreshResult{
    planResult.value().plan.revision,
    renderState.core.preparedPlanHash.value()
  };
}

foundation::Result<void> NativePreviewSession::seek(foundation::TimeSeconds time) {
  return renderSystem_.seek(time);
}

foundation::Result<void> NativePreviewSession::play() {
  return renderSystem_.play();
}

foundation::Result<void> NativePreviewSession::pause() {
  return renderSystem_.pause();
}

foundation::Result<render::RenderFrameResult> NativePreviewSession::renderFrame(render::RenderFrameRequest request) const {
  return renderSystem_.renderPlaybackFrame(render::PlaybackFrameRequest{
    request.time,
    request.quality
  });
}

render::PreviewRenderShellState NativePreviewSession::state() const {
  const render::LocalRenderSystemState renderState = renderSystem_.state();
  return render::PreviewRenderShellState{
    renderState.playback,
    renderState.playhead,
    renderState.core
  };
}

} // namespace grapple::app
