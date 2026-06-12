#include <grapple/render/PreviewRenderShell.hpp>

#include <string>

namespace grapple::render {

namespace {

foundation::Result<void> requireLoadedPlan(const LocalRenderCoreState& coreState, const char* action) {
  if (!coreState.hasPlan) {
    return foundation::Error{"render.plan_missing", std::string{"PreviewRenderShell requires a loaded RenderPlan before "} + action + "."};
  }

  return {};
}

} // namespace

PreviewRenderShell::PreviewRenderShell(LocalRenderCore& core)
  : core_{core} {}

foundation::Result<void> PreviewRenderShell::seek(foundation::TimeSeconds time) {
  auto ready = requireLoadedPlan(core_.state(), "seeking");
  if (!ready) {
    return ready;
  }

  playhead_ = time;
  return {};
}

foundation::Result<void> PreviewRenderShell::play() {
  auto ready = requireLoadedPlan(core_.state(), "playback");
  if (!ready) {
    return ready;
  }

  playback_ = PreviewPlaybackState::Playing;
  return {};
}

foundation::Result<void> PreviewRenderShell::pause() {
  auto ready = requireLoadedPlan(core_.state(), "pausing");
  if (!ready) {
    return ready;
  }

  playback_ = PreviewPlaybackState::Paused;
  return {};
}

foundation::Result<RenderFrameResult> PreviewRenderShell::renderFrame(const RenderFrameRequest& request) const {
  return core_.renderFrame(request);
}

PreviewRenderShellState PreviewRenderShell::state() const noexcept {
  return PreviewRenderShellState{
    playback_,
    playhead_,
    core_.state()
  };
}

} // namespace grapple::render
