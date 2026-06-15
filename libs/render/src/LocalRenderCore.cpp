#include <grapple/render/LocalRenderCore.hpp>

#include <grapple/projection/RenderPlanHashes.hpp>
#include <grapple/effects/OutputNames.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
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
              << " audioClips=" << sample.audioClips.size()
              << " cameras=" << sample.cameras.size()
              << " effects=" << sample.effectOutputs.size();
  return description.str();
}

RenderedMediaKind renderedMediaKindFor(timeline::ClipKind kind) {
  return kind == timeline::ClipKind::Image
    ? RenderedMediaKind::Image
    : RenderedMediaKind::Video;
}

std::uint8_t opacityAdjustedAlpha(std::uint8_t alpha, double opacity) {
  const double clampedOpacity = std::clamp(opacity, 0.0, 1.0);
  return static_cast<std::uint8_t>(std::lround(static_cast<double>(alpha) * clampedOpacity));
}

std::vector<RenderedMediaFrame> buildMediaFrames(const runtime::RuntimeSample& sample) {
  std::vector<RenderedMediaFrame> frames;
  frames.reserve(sample.clips.size());

  for (const projection::RenderClip& clip : sample.clips) {
    const timeline::ClipPayload& payload = clip.payload;
    frames.push_back(RenderedMediaFrame{
      clip.sourceNodeId,
      clip.trackNodeId,
      payload.assetId,
      renderedMediaKindFor(payload.kind),
      foundation::TimeSeconds{
        payload.sourceRange.start.value + ((sample.time.value - payload.timelineRange.start.value) * payload.playbackRate)
      },
      payload.transform
    });
  }

  return frames;
}

std::vector<RenderedAudioClip> buildAudioClips(const runtime::RuntimeSample& sample) {
  std::vector<RenderedAudioClip> audioClips;
  audioClips.reserve(sample.audioClips.size());

  for (const projection::RenderAudioClip& clip : sample.audioClips) {
    const timeline::ClipPayload& payload = clip.payload;
    audioClips.push_back(RenderedAudioClip{
      clip.sourceNodeId,
      clip.trackNodeId,
      payload.assetId,
      payload.timelineRange,
      payload.sourceRange,
      payload.playbackRate
    });
  }

  return audioClips;
}

void applyCameraTransformOutputs(
  runtime::RuntimeSample& sample,
  const foundation::ProjectId& projectId,
  const foundation::RevisionId& revision
) {
  for (const runtime::RuntimeEffectOutput& output : sample.effectOutputs) {
    for (const runtime::RuntimeNamedValue& value : output.values) {
      if (value.name != effects::output_name::CameraTransform) {
        continue;
      }

      const auto* transform = std::get_if<timeline::Transform2D>(&value.value);
      if (transform == nullptr) {
        sample.diagnostics.push_back(runtime::RuntimeDiagnostic{
          "runtime.camera_transform_output_invalid",
          runtime::DiagnosticSeverity::Error,
          runtime::DiagnosticLocation{
            projectId,
            revision,
            output.sourceNodeId
          },
          "Runtime output camera_transform must be a Transform2D value."
        });
        continue;
      }

      bool matchedCamera = false;
      for (runtime::ResolvedCamera& camera : sample.cameras) {
        if (camera.sourceNodeId == output.targetNodeId) {
          camera.state.transform = *transform;
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
      camera.state
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

std::optional<RenderedImage> sampleTransformedImage(
  std::optional<RenderedImage> image,
  const foundation::Transform2D& transform,
  foundation::Vec2 sourceOffsetPixels,
  foundation::Vec2 destinationOffsetPixels
) {
  if (!image.has_value()) {
    return image;
  }

  const int width = image->resolution.width;
  const int height = image->resolution.height;
  if (width <= 0 || height <= 0 || image->rgbaPixels.size() != static_cast<std::size_t>(width * height * 4)) {
    return image;
  }

  const double scaleX = transform.scale.x;
  const double scaleY = transform.scale.y;
  if (scaleX <= 0.0 || scaleY <= 0.0) {
    return image;
  }

  const double rotationRadians = transform.rotationDegrees * (std::acos(-1.0) / 180.0);
  const double cosTheta = std::cos(rotationRadians);
  const double sinTheta = std::sin(rotationRadians);
  if (sourceOffsetPixels.x == 0.0 &&
      sourceOffsetPixels.y == 0.0 &&
      destinationOffsetPixels.x == 0.0 &&
      destinationOffsetPixels.y == 0.0 &&
      scaleX == 1.0 &&
      scaleY == 1.0 &&
      transform.rotationDegrees == 0.0 &&
      transform.opacity == 1.0) {
    return image;
  }

  const double centerX = (static_cast<double>(width) - 1.0) * 0.5;
  const double centerY = (static_cast<double>(height) - 1.0) * 0.5;
  std::vector<std::uint8_t> transformed(image->rgbaPixels.size(), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double destinationX = static_cast<double>(x) - centerX - destinationOffsetPixels.x;
      const double destinationY = static_cast<double>(y) - centerY - destinationOffsetPixels.y;
      const double rotatedX = (cosTheta * destinationX) + (sinTheta * destinationY);
      const double rotatedY = (-sinTheta * destinationX) + (cosTheta * destinationY);
      const int sourceX = static_cast<int>(std::lround((rotatedX / scaleX) + centerX + sourceOffsetPixels.x));
      const int sourceY = static_cast<int>(std::lround((rotatedY / scaleY) + centerY + sourceOffsetPixels.y));
      if (sourceX < 0 || sourceX >= width || sourceY < 0 || sourceY >= height) {
        continue;
      }
      const std::size_t destinationIndex = static_cast<std::size_t>((y * width + x) * 4);
      const std::size_t sourceIndex = static_cast<std::size_t>((sourceY * width + sourceX) * 4);
      transformed[destinationIndex] = image->rgbaPixels[sourceIndex];
      transformed[destinationIndex + 1] = image->rgbaPixels[sourceIndex + 1];
      transformed[destinationIndex + 2] = image->rgbaPixels[sourceIndex + 2];
      transformed[destinationIndex + 3] = opacityAdjustedAlpha(image->rgbaPixels[sourceIndex + 3], transform.opacity);
    }
  }

  image->rgbaPixels = std::move(transformed);
  return image;
}

std::optional<RenderedImage> applyMediaTransformToImage(
  std::optional<RenderedImage> image,
  const std::vector<RenderedMediaFrame>& mediaFrames
) {
  if (!image.has_value() || mediaFrames.empty()) {
    return image;
  }

  const RenderedMediaFrame& mediaFrame = mediaFrames.front();
  const foundation::Transform2D& transform = mediaFrame.transform;
  const foundation::Vec2 destinationOffsetPixels{
    transform.position.x * static_cast<double>(image->resolution.width),
    -transform.position.y * static_cast<double>(image->resolution.height)
  };
  return sampleTransformedImage(
    std::move(image),
    transform,
    foundation::Vec2{},
    destinationOffsetPixels
  );
}

std::optional<RenderedImage> applyCameraTransformToImage(
  std::optional<RenderedImage> image,
  const std::vector<RenderedCamera>& cameras
) {
  if (!image.has_value() || cameras.empty()) {
    return image;
  }

  const foundation::Transform2D& transform = cameras.front().state.transform;
  const foundation::Vec2 sourceOffsetPixels{
    transform.position.x * static_cast<double>(image->resolution.width),
    transform.position.y * static_cast<double>(image->resolution.height)
  };
  return sampleTransformedImage(
    std::move(image),
    transform,
    sourceOffsetPixels,
    foundation::Vec2{}
  );
}

foundation::Result<RenderFrameResult> renderSampleFrame(
  runtime::RuntimeSample sample,
  const runtime::PreparedRuntimePlan& prepared,
  IRenderFrameSource* frameSource,
  const RenderFrameRequest& request
);

foundation::Result<RenderFrameResult> renderPreparedFrame(
  runtime::RuntimeEvaluator& runtime,
  const runtime::PreparedRuntimePlan& prepared,
  IRenderFrameSource* frameSource,
  const RenderFrameRequest& request
) {
  auto sampleResult = runtime.sample(runtime::RuntimeSampleRequest{
    prepared,
    request.time,
    runtimeQualityFor(request.quality)
  });
  if (!sampleResult) {
    return sampleResult.error();
  }

  runtime::RuntimeSample sample = std::move(sampleResult.value().sample);
  return renderSampleFrame(
    std::move(sample),
    prepared,
    frameSource,
    request
  );
}

foundation::Result<RenderFrameResult> renderSampleFrame(
  runtime::RuntimeSample sample,
  const runtime::PreparedRuntimePlan& prepared,
  IRenderFrameSource* frameSource,
  const RenderFrameRequest& request
) {
  applyCameraTransformOutputs(
    sample,
    prepared.dependencyGraph.projectId,
    prepared.sourceRevision
  );

  const std::vector<RenderedMediaFrame> mediaFrames = buildMediaFrames(sample);
  const std::vector<RenderedAudioClip> audioClips = buildAudioClips(sample);
  const std::vector<RenderedCamera> cameras = buildRenderedCameras(sample);
  auto image = buildRenderedImage(mediaFrames, request.quality, frameSource);
  if (!image) {
    return image.error();
  }
  std::optional<RenderedImage> transformedImage = applyMediaTransformToImage(std::move(image.value()), mediaFrames);
  transformedImage = applyCameraTransformToImage(std::move(transformedImage), cameras);

  return RenderFrameResult{
    RenderFrame{
      prepared.sourceRevision,
      prepared.planHash,
      request.time,
      describeSample(sample),
      mediaFrames,
      audioClips,
      cameras,
      std::move(transformedImage)
    },
    sample.diagnostics,
    {}
  };
}

} // namespace

LocalRenderCore::LocalRenderCore(runtime::RuntimeEvaluator& runtime)
  : runtime_{runtime} {}

LocalRenderCore::LocalRenderCore(runtime::RuntimeEvaluator& runtime, IRenderFrameSource& frameSource)
  : runtime_{runtime},
    frameSource_{&frameSource} {}

foundation::Result<void> LocalRenderCore::loadPlan(const projection::RenderPlan& plan) {
  const foundation::Hash256 planHash = projection::hashRenderPlan(plan);
  if (prepared_.has_value() &&
      state_.preparedPlanHash.has_value() &&
      state_.preparedPlanHash.value() == planHash) {
    state_.hasPlan = true;
    state_.revision = plan.revision;
    prepared_->sourceRevision = plan.revision;
    return {};
  }

  auto prepared = runtime_.prepare(runtime::PrepareRuntimePlanRequest{
    plan,
    prepared_.has_value() ? &prepared_.value() : nullptr
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

  return renderPreparedFrame(runtime_, prepared_.value(), frameSource_, request);
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

  std::vector<runtime::RuntimeDiagnostic> diagnostics = range.value().diagnostics;
  const std::size_t preparedDiagnosticCount = range.value().diagnostics.size();

  for (runtime::RuntimeFrameResult& runtimeFrame : range.value().frames) {
    const foundation::TimeSeconds frameTime = runtimeFrame.sample.time;
    auto frameResult = renderSampleFrame(
      std::move(runtimeFrame.sample),
      prepared_.value(),
      frameSource_,
      RenderFrameRequest{
        frameTime,
        request.quality
      }
    );
    if (!frameResult) {
      return frameResult.error();
    }

    const auto frameIndex = static_cast<std::size_t>(runtimeFrame.frame.value);
    if (request.sink != nullptr) {
      auto writeFrame = request.sink->writeFrame(frameIndex, frameResult.value());
      if (!writeFrame) {
        return writeFrame.error();
      }
    }

    const std::vector<runtime::RuntimeDiagnostic>& frameDiagnostics = frameResult.value().runtimeDiagnostics;
    const std::size_t firstFrameDiagnostic = std::min(preparedDiagnosticCount, frameDiagnostics.size());
    diagnostics.insert(
      diagnostics.end(),
      frameDiagnostics.begin() + static_cast<std::ptrdiff_t>(firstFrameDiagnostic),
      frameDiagnostics.end()
    );
  }

  return RenderRangeResult{
    prepared_->sourceRevision,
    prepared_->planHash,
    range.value().frames.size(),
    diagnostics,
    {}
  };
}

LocalRenderCoreState LocalRenderCore::state() const noexcept {
  return state_;
}

} // namespace grapple::render
