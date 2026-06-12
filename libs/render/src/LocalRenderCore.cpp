#include <grapple/render/LocalRenderCore.hpp>

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
              << " cameras=" << sample.cameras.size()
              << " effects=" << sample.effectOutputs.size();
  return description.str();
}

bool isVisualClipKind(timeline::ClipKind kind) {
  return kind == timeline::ClipKind::Video || kind == timeline::ClipKind::Image;
}

RenderedMediaKind renderedMediaKindFor(timeline::ClipKind kind) {
  return kind == timeline::ClipKind::Image
    ? RenderedMediaKind::Image
    : RenderedMediaKind::Video;
}

std::vector<RenderedMediaFrame> buildMediaFrames(const runtime::RuntimeSample& sample) {
  std::vector<RenderedMediaFrame> frames;
  frames.reserve(sample.clips.size());

  for (const projection::RenderClip& clip : sample.clips) {
    const timeline::ClipPayload& payload = clip.payload;
    if (!isVisualClipKind(payload.kind)) {
      continue;
    }

    frames.push_back(RenderedMediaFrame{
      clip.sourceNodeId,
      clip.trackNodeId,
      payload.assetId,
      renderedMediaKindFor(payload.kind),
      foundation::TimeSeconds{
        payload.sourceRange.start.value + ((sample.time.value - payload.timelineRange.start.value) * payload.playbackRate)
      }
    });
  }

  return frames;
}

foundation::Result<std::optional<RenderedImage>> buildRenderedImage(
  const std::vector<RenderedMediaFrame>& mediaFrames,
  RenderQuality quality,
  IRenderFrameSource* frameSource
) {
  if (frameSource == nullptr || mediaFrames.empty()) {
    return std::optional<RenderedImage>{};
  }

  const RenderedMediaFrame& firstFrame = mediaFrames.front();
  auto sourceFrame = frameSource->frameAt(SourceFrameRequest{
    firstFrame.assetId,
    firstFrame.sourceTime,
    quality
  });
  if (!sourceFrame) {
    return sourceFrame.error();
  }

  return std::optional<RenderedImage>{RenderedImage{
    sourceFrame.value().resolution,
    std::move(sourceFrame.value().rgbaPixels)
  }};
}

} // namespace

LocalRenderCore::LocalRenderCore(runtime::RuntimeEvaluator& runtime)
  : runtime_{runtime} {}

LocalRenderCore::LocalRenderCore(runtime::RuntimeEvaluator& runtime, IRenderFrameSource& frameSource)
  : runtime_{runtime},
    frameSource_{&frameSource} {}

foundation::Result<void> LocalRenderCore::loadPlan(const projection::RenderPlan& plan) {
  auto prepared = runtime_.prepare(runtime::PrepareRuntimePlanRequest{
    plan
  });
  if (!prepared) {
    return prepared.error();
  }

  state_.hasPlan = true;
  state_.revision = plan.revision;
  state_.preparedPlanHash = prepared.value().prepared.planHash;
  prepared_ = std::move(prepared.value().prepared);
  return {};
}

foundation::Result<RenderFrameResult> LocalRenderCore::renderFrame(const RenderFrameRequest& request) const {
  if (!prepared_.has_value()) {
    return foundation::Error{"render.plan_missing", "LocalRenderCore requires a loaded RenderPlan before rendering a frame."};
  }

  auto sample = runtime_.sample(runtime::RuntimeSampleRequest{
    prepared_.value(),
    request.time,
    runtimeQualityFor(request.quality)
  });
  if (!sample) {
    return sample.error();
  }

  const std::vector<RenderedMediaFrame> mediaFrames = buildMediaFrames(sample.value().sample);
  auto image = buildRenderedImage(mediaFrames, request.quality, frameSource_);
  if (!image) {
    return image.error();
  }

  return RenderFrameResult{
    RenderFrame{
      request.time,
      describeSample(sample.value().sample),
      mediaFrames,
      std::move(image.value())
    },
    sample.value().diagnostics,
    {}
  };
}

foundation::Result<RenderRangeResult> LocalRenderCore::renderRange(const RenderRangeRequest& request) const {
  if (!prepared_.has_value()) {
    return foundation::Error{"render.plan_missing", "LocalRenderCore requires a loaded RenderPlan before rendering a range."};
  }

  auto range = runtime_.evaluateRange(runtime::RuntimeRangeRequest{
    prepared_.value(),
    request.range,
    request.frameRate,
    runtimeQualityFor(request.quality)
  });
  if (!range) {
    return range.error();
  }

  return RenderRangeResult{
    range.value().frames.size(),
    range.value().diagnostics,
    {}
  };
}

LocalRenderCoreState LocalRenderCore::state() const noexcept {
  return state_;
}

} // namespace grapple::render
