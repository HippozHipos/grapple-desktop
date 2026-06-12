#include <grapple/render/PreviewSession.hpp>

namespace grapple::render {

PreviewSession::PreviewSession(runtime::RuntimeEvaluator& runtime)
  : runtime_(runtime) {}

foundation::Result<void> PreviewSession::loadPlan(const projection::RenderPlan& plan) {
  auto prepared = runtime_.prepare(runtime::PrepareRuntimePlanRequest{
    plan,
    runtime::RuntimePrepareMode::Interactive
  });
  if (!prepared) {
    return prepared.error();
  }

  state_.hasPlan = true;
  state_.playback = PlaybackState::Paused;
  state_.playhead = foundation::TimeSeconds{0.0};
  state_.revision = plan.revision;
  state_.preparedPlanHash = prepared.value().prepared.planHash;
  return {};
}

foundation::Result<void> PreviewSession::seek(foundation::TimeSeconds time) {
  if (!state_.hasPlan) {
    return foundation::Error{"render.preview_plan_missing", "PreviewSession requires a loaded RenderPlan before seeking."};
  }

  state_.playhead = time;
  return {};
}

foundation::Result<void> PreviewSession::play() {
  if (!state_.hasPlan) {
    return foundation::Error{"render.preview_plan_missing", "PreviewSession requires a loaded RenderPlan before playback."};
  }

  state_.playback = PlaybackState::Playing;
  return {};
}

foundation::Result<void> PreviewSession::pause() {
  if (!state_.hasPlan) {
    return foundation::Error{"render.preview_plan_missing", "PreviewSession requires a loaded RenderPlan before pausing."};
  }

  state_.playback = PlaybackState::Paused;
  return {};
}

foundation::Result<PreviewState> PreviewSession::state() const {
  return state_;
}

} // namespace grapple::render

