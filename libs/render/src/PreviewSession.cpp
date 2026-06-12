#include <grapple/render/PreviewSession.hpp>

#include <sstream>
#include <utility>

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
  prepared_ = std::move(prepared.value().prepared);
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

foundation::Result<PreviewRenderFrameResult> PreviewSession::renderFrame(
  const PreviewRenderFrameRequest& request
) const {
  if (!prepared_.has_value()) {
    return foundation::Error{"render.preview_plan_missing", "PreviewSession requires a loaded RenderPlan before rendering a frame."};
  }

  const runtime::RuntimeQuality runtimeQuality = request.quality == PreviewQuality::Full
    ? runtime::RuntimeQuality::Final
    : runtime::RuntimeQuality::Interactive;
  auto sample = runtime_.sample(runtime::RuntimeSampleRequest{
    prepared_.value(),
    request.time,
    runtimeQuality
  });
  if (!sample) {
    return sample.error();
  }

  std::ostringstream description;
  description << "layers=" << sample.value().sample.layers.size()
              << " clips=" << sample.value().sample.clips.size()
              << " cameras=" << sample.value().sample.cameras.size();

  return PreviewRenderFrameResult{
    PreviewFrame{request.time, description.str()},
    sample.value().diagnostics,
    {}
  };
}

foundation::Result<PreviewState> PreviewSession::state() const {
  return state_;
}

} // namespace grapple::render
