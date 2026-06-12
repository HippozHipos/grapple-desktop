#include <grapple/render/LocalRenderSystem.hpp>

#include <sstream>
#include <utility>

namespace grapple::render {

namespace {

runtime::RuntimeQuality runtimeQualityFor(RenderQuality quality) {
  return quality == RenderQuality::Final
    ? runtime::RuntimeQuality::Final
    : runtime::RuntimeQuality::Interactive;
}

std::string describeSample(const runtime::RuntimeSample& sample) {
  std::ostringstream description;
  description << "layers=" << sample.layers.size()
              << " clips=" << sample.clips.size()
              << " cameras=" << sample.cameras.size();
  return description.str();
}

} // namespace

LocalRenderSystem::LocalRenderSystem(runtime::RuntimeEvaluator& runtime)
  : runtime_(runtime) {}

foundation::Result<void> LocalRenderSystem::loadPlan(const projection::RenderPlan& plan) {
  auto prepared = runtime_.prepare(runtime::PrepareRuntimePlanRequest{
    plan
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

foundation::Result<void> LocalRenderSystem::seek(foundation::TimeSeconds time) {
  if (!state_.hasPlan) {
    return foundation::Error{"render.plan_missing", "LocalRenderSystem requires a loaded RenderPlan before seeking."};
  }

  state_.playhead = time;
  return {};
}

foundation::Result<void> LocalRenderSystem::play() {
  if (!state_.hasPlan) {
    return foundation::Error{"render.plan_missing", "LocalRenderSystem requires a loaded RenderPlan before playback."};
  }

  state_.playback = PlaybackState::Playing;
  return {};
}

foundation::Result<void> LocalRenderSystem::pause() {
  if (!state_.hasPlan) {
    return foundation::Error{"render.plan_missing", "LocalRenderSystem requires a loaded RenderPlan before pausing."};
  }

  state_.playback = PlaybackState::Paused;
  return {};
}

foundation::Result<PlaybackFrameResult> LocalRenderSystem::renderPlaybackFrame(
  const PlaybackFrameRequest& request
) const {
  if (!prepared_.has_value()) {
    return foundation::Error{"render.plan_missing", "LocalRenderSystem requires a loaded RenderPlan before rendering a playback frame."};
  }

  auto sample = runtime_.sample(runtime::RuntimeSampleRequest{
    prepared_.value(),
    request.time,
    runtimeQualityFor(request.quality)
  });
  if (!sample) {
    return sample.error();
  }

  return PlaybackFrameResult{
    PlaybackFrame{request.time, describeSample(sample.value().sample)},
    sample.value().diagnostics,
    {}
  };
}

foundation::Result<ExportResult> LocalRenderSystem::exportRange(const ExportRequest& request) {
  if (!prepared_.has_value()) {
    return foundation::Error{"render.plan_missing", "LocalRenderSystem requires a loaded RenderPlan before export."};
  }

  auto range = runtime_.evaluateRange(runtime::RuntimeRangeRequest{
    prepared_.value(),
    request.settings.range,
    request.settings.frameRate,
    runtimeQualityFor(request.settings.quality)
  });
  if (!range) {
    return range.error();
  }

  state_.lastExportSettings = request.settings;
  state_.lastExportOutputPath = request.settings.outputPath;
  return ExportResult{
    request.settings.outputPath,
    range.value().frames.size(),
    range.value().diagnostics,
    {}
  };
}

foundation::Result<LocalRenderSystemState> LocalRenderSystem::state() const {
  return state_;
}

} // namespace grapple::render
