#include <grapple/app/NativePreviewSession.hpp>

namespace grapple::app {

NativePreviewSession::NativePreviewSession(
  NativeProjectSession& project,
  render::LocalRenderCore& core
) : project_{project},
    core_{core},
    preview_{core_} {}

foundation::Result<NativePreviewRefreshResult> NativePreviewSession::refreshFromProject() {
  auto planResult = project_.buildRenderPlan();
  if (!planResult) {
    return planResult.error();
  }

  auto loadResult = core_.loadPlan(planResult.value().plan);
  if (!loadResult) {
    return loadResult.error();
  }

  const render::LocalRenderCoreState coreState = core_.state();
  return NativePreviewRefreshResult{
    planResult.value().plan.revision,
    coreState.preparedPlanHash.value()
  };
}

foundation::Result<void> NativePreviewSession::seek(foundation::TimeSeconds time) {
  return preview_.seek(time);
}

foundation::Result<void> NativePreviewSession::play() {
  return preview_.play();
}

foundation::Result<void> NativePreviewSession::pause() {
  return preview_.pause();
}

foundation::Result<render::RenderFrameResult> NativePreviewSession::renderFrame(render::RenderFrameRequest request) const {
  return preview_.renderFrame(request);
}

render::PreviewRenderShellState NativePreviewSession::state() const noexcept {
  return preview_.state();
}

} // namespace grapple::app
