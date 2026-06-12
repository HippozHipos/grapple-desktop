#include <grapple/render/LocalRenderCore.hpp>

#include <grapple/runtime/RuntimeOutputNames.hpp>

#include <algorithm>
#include <cstddef>
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

void applyCameraTransformOutputs(
  runtime::RuntimeSample& sample,
  const foundation::ProjectId& projectId,
  const foundation::RevisionId& revision
) {
  for (const runtime::RuntimeEffectOutput& output : sample.effectOutputs) {
    for (const runtime::RuntimeNamedValue& value : output.values) {
      if (value.name != runtime::output_name::CameraTransform) {
        continue;
      }

      const auto* transform = std::get_if<timeline::Transform>(&value.value);
      if (transform == nullptr) {
        sample.diagnostics.push_back(runtime::RuntimeDiagnostic{
          "runtime.camera_transform_output_invalid",
          runtime::DiagnosticSeverity::Error,
          runtime::DiagnosticLocation{
            projectId,
            revision,
            output.sourceNodeId
          },
          "Runtime output camera_transform must be a Transform value."
        });
        continue;
      }

      bool matchedCamera = false;
      for (runtime::ResolvedCamera& camera : sample.cameras) {
        if (camera.sourceNodeId == output.targetNodeId) {
          camera.transform = *transform;
          matchedCamera = true;
        }
      }
      if (!matchedCamera) {
        sample.diagnostics.push_back(runtime::RuntimeDiagnostic{
          "runtime.camera_transform_target_missing",
          runtime::DiagnosticSeverity::Error,
          runtime::DiagnosticLocation{
            projectId,
            revision,
            output.sourceNodeId
          },
          "Runtime output camera_transform must target a camera node."
        });
      }
    }
  }
}

std::vector<RenderedCamera> buildRenderedCameras(const runtime::RuntimeSample& sample) {
  std::vector<RenderedCamera> cameras;
  cameras.reserve(sample.cameras.size());

  for (const runtime::ResolvedCamera& camera : sample.cameras) {
    cameras.push_back(RenderedCamera{
      camera.sourceNodeId,
      camera.transform,
      camera.lens
    });
  }

  return cameras;
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

  auto sampleResult = runtime_.sample(runtime::RuntimeSampleRequest{
    prepared_.value(),
    request.time,
    runtimeQualityFor(request.quality)
  });
  if (!sampleResult) {
    return sampleResult.error();
  }

  runtime::RuntimeSample sample = std::move(sampleResult.value().sample);
  applyCameraTransformOutputs(
    sample,
    prepared_.value().dependencyGraph.projectId,
    prepared_.value().sourceRevision
  );

  const std::vector<RenderedMediaFrame> mediaFrames = buildMediaFrames(sample);
  const std::vector<RenderedCamera> cameras = buildRenderedCameras(sample);
  auto image = buildRenderedImage(mediaFrames, request.quality, frameSource_);
  if (!image) {
    return image.error();
  }

  return RenderFrameResult{
    RenderFrame{
      request.time,
      describeSample(sample),
      mediaFrames,
      cameras,
      std::move(image.value())
    },
    sample.diagnostics,
    {}
  };
}

foundation::Result<RenderRangeResult> LocalRenderCore::renderRange(const RenderRangeRequest& request) const {
  if (!prepared_.has_value()) {
    return foundation::Error{"render.plan_missing", "LocalRenderCore requires a loaded RenderPlan before rendering a range."};
  }

  std::vector<runtime::RuntimeDiagnostic> diagnostics = prepared_.value().diagnostics;
  const std::size_t preparedDiagnosticCount = prepared_.value().diagnostics.size();
  const double framesPerSecond = request.frameRate.framesPerSecond();
  const double duration = request.range.duration();
  const auto frameCount = static_cast<std::size_t>(duration * framesPerSecond);

  for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
    const foundation::TimeSeconds time{
      request.range.start.value + static_cast<double>(frameIndex) / framesPerSecond
    };
    auto sampleResult = runtime_.sample(runtime::RuntimeSampleRequest{
      prepared_.value(),
      time,
      runtimeQualityFor(request.quality)
    });
    if (!sampleResult) {
      return sampleResult.error();
    }

    runtime::RuntimeSample sample = std::move(sampleResult.value().sample);
    applyCameraTransformOutputs(
      sample,
      prepared_.value().dependencyGraph.projectId,
      prepared_.value().sourceRevision
    );
    const std::size_t firstFrameDiagnostic = std::min(preparedDiagnosticCount, sample.diagnostics.size());
    diagnostics.insert(
      diagnostics.end(),
      sample.diagnostics.begin() + static_cast<std::ptrdiff_t>(firstFrameDiagnostic),
      sample.diagnostics.end()
    );
  }

  return RenderRangeResult{
    frameCount,
    diagnostics,
    {}
  };
}

LocalRenderCoreState LocalRenderCore::state() const noexcept {
  return state_;
}

} // namespace grapple::render
