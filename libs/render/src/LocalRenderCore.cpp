#include <grapple/render/LocalRenderCore.hpp>

#include <grapple/projection/RenderPlanHashes.hpp>
#include <grapple/effects/OutputNames.hpp>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <sstream>
#include <utility>
#include <variant>

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
              << " textClips=" << sample.textClips.size()
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

struct ClipTint {
  foundation::Vec3 color;
  double amount = 0.0;
};

struct ClipExposure {
  double exposure = 0.0;
};

std::uint8_t opacityAdjustedAlpha(std::uint8_t alpha, double opacity) {
  const double clampedOpacity = std::clamp(opacity, 0.0, 1.0);
  return static_cast<std::uint8_t>(std::lround(static_cast<double>(alpha) * clampedOpacity));
}

foundation::Result<RenderedImage> renderedImageFromSourceFrame(SourceFrame frame) {
  if (frame.resolution.width <= 0 || frame.resolution.height <= 0) {
    return foundation::Error{
      "render.source_frame_invalid",
      "Render frame source must return RGBA pixels matching its declared resolution."
    };
  }

  const std::size_t expectedBytes =
    static_cast<std::size_t>(frame.resolution.width) *
    static_cast<std::size_t>(frame.resolution.height) *
    4;
  if (frame.rgbaPixels.size() != expectedBytes) {
    return foundation::Error{
      "render.source_frame_invalid",
      "Render frame source must return RGBA pixels matching its declared resolution."
    };
  }

  return RenderedImage{
    frame.resolution,
    std::move(frame.rgbaPixels)
  };
}

std::vector<ClipTint> clipTintsFor(
  runtime::RuntimeSample& sample,
  const foundation::NodeId& clipNodeId,
  const foundation::ProjectId& projectId,
  const foundation::RevisionId& revision
) {
  std::vector<ClipTint> tints;
  for (const runtime::RuntimeEffectOutput& output : sample.effectOutputs) {
    if (output.targetNodeId != clipNodeId) {
      continue;
    }

    std::optional<foundation::Vec3> color;
    std::optional<double> amount;
    for (const runtime::RuntimeNamedValue& value : output.values) {
      if (value.name == effects::output_name::ClipTint) {
        if (const auto* vector = std::get_if<foundation::Vec3>(&value.value)) {
          color = *vector;
        }
      } else if (value.name == effects::output_name::ClipTintAmount) {
        if (const auto* numeric = std::get_if<double>(&value.value)) {
          amount = *numeric;
        }
      }
    }
    if (color.has_value() && amount.has_value()) {
      tints.push_back(ClipTint{color.value(), amount.value()});
      continue;
    }

    const bool hadTintOutput =
      std::any_of(output.values.begin(), output.values.end(), [](const runtime::RuntimeNamedValue& value) {
        return value.name == effects::output_name::ClipTint ||
               value.name == effects::output_name::ClipTintAmount;
      });
    if (hadTintOutput) {
      sample.diagnostics.push_back(runtime::RuntimeDiagnostic{
        "runtime.clip_tint_output_invalid",
        runtime::DiagnosticSeverity::Error,
        runtime::DiagnosticLocation{
          projectId,
          revision,
          output.sourceNodeId
        },
        "Runtime clip tint output requires clip_tint Vec3 and clip_tint_amount numeric values."
      });
    }
  }
  return tints;
}

std::vector<ClipExposure> clipExposuresFor(
  runtime::RuntimeSample& sample,
  const foundation::NodeId& clipNodeId,
  const foundation::ProjectId& projectId,
  const foundation::RevisionId& revision
) {
  std::vector<ClipExposure> exposures;
  for (const runtime::RuntimeEffectOutput& output : sample.effectOutputs) {
    if (output.targetNodeId != clipNodeId) {
      continue;
    }

    for (const runtime::RuntimeNamedValue& value : output.values) {
      if (value.name != effects::output_name::ClipExposure) {
        continue;
      }
      if (const auto* numeric = std::get_if<double>(&value.value)) {
        exposures.push_back(ClipExposure{*numeric});
      } else {
        sample.diagnostics.push_back(runtime::RuntimeDiagnostic{
          "runtime.clip_exposure_output_invalid",
          runtime::DiagnosticSeverity::Error,
          runtime::DiagnosticLocation{
            projectId,
            revision,
            output.sourceNodeId
          },
          "Runtime clip exposure output requires a numeric clip_exposure value."
        });
      }
    }
  }
  return exposures;
}

std::vector<RenderedMediaFrame> buildMediaFrames(
  runtime::RuntimeSample& sample,
  const foundation::ProjectId& projectId,
  const foundation::RevisionId& revision
) {
  std::vector<RenderedMediaFrame> frames;
  frames.reserve(sample.clips.size());

  for (const projection::RenderClip& clip : sample.clips) {
    const timeline::ClipPayload& payload = clip.payload;
    const std::vector<ClipTint> tints = clipTintsFor(sample, clip.sourceNodeId, projectId, revision);
    const std::vector<ClipExposure> exposures = clipExposuresFor(sample, clip.sourceNodeId, projectId, revision);
    std::optional<foundation::Vec3> tintColor;
    double tintAmount = 0.0;
    for (const ClipTint& tint : tints) {
      tintColor = tint.color;
      tintAmount = std::clamp(tintAmount + tint.amount, 0.0, 1.0);
    }
    double exposure = 0.0;
    for (const ClipExposure& clipExposure : exposures) {
      exposure = std::clamp(exposure + clipExposure.exposure, -2.0, 2.0);
    }

    frames.push_back(RenderedMediaFrame{
      clip.sourceNodeId,
      clip.trackNodeId,
      payload.assetId,
      renderedMediaKindFor(payload.kind),
      foundation::TimeSeconds{
        payload.sourceRange.start.value + ((sample.time.value - payload.timelineRange.start.value) * payload.playbackRate)
      },
      payload.transform,
      tintColor,
      tintAmount,
      exposure
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

std::vector<RenderedTextFrame> buildTextFrames(const runtime::RuntimeSample& sample) {
  std::vector<RenderedTextFrame> textFrames;
  textFrames.reserve(sample.textClips.size());

  for (const projection::RenderTextClip& clip : sample.textClips) {
    const timeline::TextClipPayload& payload = clip.payload;
    textFrames.push_back(RenderedTextFrame{
      clip.sourceNodeId,
      clip.trackNodeId,
      payload.text,
      payload.transform,
      payload.style
    });
  }

  return textFrames;
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

RenderedImage sampleTransformedImage(
  const RenderedImage& image,
  foundation::Resolution targetResolution,
  const foundation::Transform2D& transform,
  foundation::Vec2 sourceOffsetPixels,
  foundation::Vec2 destinationOffsetPixels
) {
  const double scaleX = transform.scale.x;
  const double scaleY = transform.scale.y;
  const int sourceWidth = image.resolution.width;
  const int sourceHeight = image.resolution.height;
  const int targetWidth = targetResolution.width;
  const int targetHeight = targetResolution.height;

  RenderedImage transformed{
    targetResolution,
    std::vector<std::uint8_t>(static_cast<std::size_t>(targetWidth * targetHeight * 4), 0)
  };

  if (scaleX <= 0.0 || scaleY <= 0.0) {
    return transformed;
  }

  const double rotationRadians = transform.rotationDegrees * (std::acos(-1.0) / 180.0);
  const double cosTheta = std::cos(rotationRadians);
  const double sinTheta = std::sin(rotationRadians);
  const double sourceCenterX = (static_cast<double>(sourceWidth) - 1.0) * 0.5;
  const double sourceCenterY = (static_cast<double>(sourceHeight) - 1.0) * 0.5;
  const double destinationCenterX = (static_cast<double>(targetWidth) - 1.0) * 0.5;
  const double destinationCenterY = (static_cast<double>(targetHeight) - 1.0) * 0.5;

  for (int y = 0; y < targetHeight; ++y) {
    for (int x = 0; x < targetWidth; ++x) {
      const double destinationX = static_cast<double>(x) - destinationCenterX - destinationOffsetPixels.x;
      const double destinationY = static_cast<double>(y) - destinationCenterY - destinationOffsetPixels.y;
      const double rotatedX = (cosTheta * destinationX) + (sinTheta * destinationY);
      const double rotatedY = (-sinTheta * destinationX) + (cosTheta * destinationY);
      const int sourceX = static_cast<int>(std::lround((rotatedX / scaleX) + sourceCenterX + sourceOffsetPixels.x));
      const int sourceY = static_cast<int>(std::lround((rotatedY / scaleY) + sourceCenterY + sourceOffsetPixels.y));
      if (sourceX < 0 || sourceX >= sourceWidth || sourceY < 0 || sourceY >= sourceHeight) {
        continue;
      }
      const std::size_t destinationIndex = static_cast<std::size_t>((y * targetWidth + x) * 4);
      const std::size_t sourceIndex = static_cast<std::size_t>((sourceY * sourceWidth + sourceX) * 4);
      transformed.rgbaPixels[destinationIndex] = image.rgbaPixels[sourceIndex];
      transformed.rgbaPixels[destinationIndex + 1] = image.rgbaPixels[sourceIndex + 1];
      transformed.rgbaPixels[destinationIndex + 2] = image.rgbaPixels[sourceIndex + 2];
      transformed.rgbaPixels[destinationIndex + 3] = opacityAdjustedAlpha(image.rgbaPixels[sourceIndex + 3], transform.opacity);
    }
  }

  return transformed;
}

void compositePixelOver(
  std::vector<std::uint8_t>& destinationPixels,
  const std::vector<std::uint8_t>& sourcePixels,
  std::size_t pixelIndex
) {
  const double sourceAlpha = static_cast<double>(sourcePixels[pixelIndex + 3]) / 255.0;
  if (sourceAlpha <= 0.0) {
    return;
  }

  const double destinationAlpha = static_cast<double>(destinationPixels[pixelIndex + 3]) / 255.0;
  const double outputAlpha = sourceAlpha + (destinationAlpha * (1.0 - sourceAlpha));
  if (outputAlpha <= 0.0) {
    return;
  }

  for (std::size_t channel = 0; channel < 3; ++channel) {
    const double sourceColor = static_cast<double>(sourcePixels[pixelIndex + channel]);
    const double destinationColor = static_cast<double>(destinationPixels[pixelIndex + channel]);
    const double outputColor =
      ((sourceColor * sourceAlpha) + (destinationColor * destinationAlpha * (1.0 - sourceAlpha))) / outputAlpha;
    destinationPixels[pixelIndex + channel] = static_cast<std::uint8_t>(std::lround(std::clamp(outputColor, 0.0, 255.0)));
  }
  destinationPixels[pixelIndex + 3] = static_cast<std::uint8_t>(std::lround(outputAlpha * 255.0));
}

void compositeImageOver(RenderedImage& destination, const RenderedImage& source) {
  const std::size_t pixelCount = static_cast<std::size_t>(destination.resolution.width * destination.resolution.height);
  for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
    compositePixelOver(destination.rgbaPixels, source.rgbaPixels, pixel * 4);
  }
}

void compositeImageAt(RenderedImage& destination, const RenderedImage& source, int left, int top) {
  for (int y = 0; y < source.resolution.height; ++y) {
    const int destinationY = top + y;
    if (destinationY < 0 || destinationY >= destination.resolution.height) {
      continue;
    }
    for (int x = 0; x < source.resolution.width; ++x) {
      const int destinationX = left + x;
      if (destinationX < 0 || destinationX >= destination.resolution.width) {
        continue;
      }

      const std::size_t sourceIndex = static_cast<std::size_t>((y * source.resolution.width + x) * 4);
      const std::size_t destinationIndex =
        static_cast<std::size_t>((destinationY * destination.resolution.width + destinationX) * 4);
      const double sourceAlpha = static_cast<double>(source.rgbaPixels[sourceIndex + 3]) / 255.0;
      if (sourceAlpha <= 0.0) {
        continue;
      }

      const double destinationAlpha = static_cast<double>(destination.rgbaPixels[destinationIndex + 3]) / 255.0;
      const double outputAlpha = sourceAlpha + (destinationAlpha * (1.0 - sourceAlpha));
      if (outputAlpha <= 0.0) {
        continue;
      }

      for (std::size_t channel = 0; channel < 3; ++channel) {
        const double sourceColor = static_cast<double>(source.rgbaPixels[sourceIndex + channel]);
        const double destinationColor = static_cast<double>(destination.rgbaPixels[destinationIndex + channel]);
        const double outputColor =
          ((sourceColor * sourceAlpha) + (destinationColor * destinationAlpha * (1.0 - sourceAlpha))) / outputAlpha;
        destination.rgbaPixels[destinationIndex + channel] =
          static_cast<std::uint8_t>(std::lround(std::clamp(outputColor, 0.0, 255.0)));
      }
      destination.rgbaPixels[destinationIndex + 3] = static_cast<std::uint8_t>(std::lround(outputAlpha * 255.0));
    }
  }
}

RenderedImage transformedMediaImage(
  const RenderedImage& image,
  const RenderedMediaFrame& mediaFrame,
  foundation::Resolution canvasResolution
) {
  const foundation::Transform2D& transform = mediaFrame.transform;
  timeline::Transform2D canvasTransform = transform;
  canvasTransform.scale.x *= static_cast<double>(canvasResolution.width) /
                             static_cast<double>(image.resolution.width);
  canvasTransform.scale.y *= static_cast<double>(canvasResolution.height) /
                             static_cast<double>(image.resolution.height);
  const foundation::Vec2 destinationOffsetPixels{
    transform.position.x * static_cast<double>(canvasResolution.width),
    -transform.position.y * static_cast<double>(canvasResolution.height)
  };
  return sampleTransformedImage(
    image,
    canvasResolution,
    canvasTransform,
    foundation::Vec2{},
    destinationOffsetPixels
  );
}

RenderedImage tintedImage(RenderedImage image, const RenderedMediaFrame& mediaFrame) {
  if (!mediaFrame.tintColor.has_value() || mediaFrame.tintAmount <= 0.0) {
    return image;
  }

  const double amount = std::clamp(mediaFrame.tintAmount, 0.0, 1.0);
  const foundation::Vec3 color{
    std::clamp(mediaFrame.tintColor->x, 0.0, 1.0) * 255.0,
    std::clamp(mediaFrame.tintColor->y, 0.0, 1.0) * 255.0,
    std::clamp(mediaFrame.tintColor->z, 0.0, 1.0) * 255.0
  };
  const std::size_t pixelCount =
    static_cast<std::size_t>(image.resolution.width * image.resolution.height);
  for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const std::size_t index = pixel * 4;
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const double target =
        channel == 0 ? color.x : channel == 1 ? color.y : color.z;
      const double source = static_cast<double>(image.rgbaPixels[index + channel]);
      image.rgbaPixels[index + channel] =
        static_cast<std::uint8_t>(std::lround(std::clamp((source * (1.0 - amount)) + (target * amount), 0.0, 255.0)));
    }
  }
  return image;
}

RenderedImage exposedImage(RenderedImage image, const RenderedMediaFrame& mediaFrame) {
  if (std::abs(mediaFrame.exposure) < 0.0001) {
    return image;
  }

  const double multiplier = std::pow(2.0, std::clamp(mediaFrame.exposure, -2.0, 2.0));
  const std::size_t pixelCount =
    static_cast<std::size_t>(image.resolution.width * image.resolution.height);
  for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const std::size_t index = pixel * 4;
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const double source = static_cast<double>(image.rgbaPixels[index + channel]);
      image.rgbaPixels[index + channel] =
        static_cast<std::uint8_t>(std::lround(std::clamp(source * multiplier, 0.0, 255.0)));
    }
  }
  return image;
}

RenderedImage rasterizeTextFrame(const RenderedTextFrame& textFrame) {
  const double scaleMultiplier =
    std::max(0.01, (std::abs(textFrame.transform.scale.x) + std::abs(textFrame.transform.scale.y)) * 0.5);
  const double fontScale = std::max(0.1, (textFrame.style.fontSize * scaleMultiplier) / 30.0);
  const int thickness = std::max(1, static_cast<int>(std::lround(fontScale * 2.0)));
  int baseline = 0;
  const cv::Size textSize = cv::getTextSize(
    textFrame.text,
    cv::FONT_HERSHEY_SIMPLEX,
    fontScale,
    thickness,
    &baseline
  );
  const int padding = std::max(4, thickness * 4);
  const int localWidth = std::max(1, textSize.width + (padding * 2));
  const int localHeight = std::max(1, textSize.height + baseline + (padding * 2));
  RenderedImage local{
    foundation::Resolution{localWidth, localHeight},
    std::vector<std::uint8_t>(static_cast<std::size_t>(localWidth * localHeight * 4), 0)
  };

  cv::Mat localMat{
    localHeight,
    localWidth,
    CV_8UC4,
    local.rgbaPixels.data()
  };
  const cv::Scalar color{
    std::clamp(textFrame.style.color.x, 0.0, 1.0) * 255.0,
    std::clamp(textFrame.style.color.y, 0.0, 1.0) * 255.0,
    std::clamp(textFrame.style.color.z, 0.0, 1.0) * 255.0,
    std::clamp(textFrame.transform.opacity, 0.0, 1.0) * 255.0
  };
  cv::putText(
    localMat,
    textFrame.text,
    cv::Point{padding, padding + textSize.height},
    cv::FONT_HERSHEY_SIMPLEX,
    fontScale,
    color,
    thickness,
    cv::LINE_AA
  );

  if (std::abs(textFrame.transform.rotationDegrees) < 0.0001) {
    return local;
  }

  const cv::Point2f center{static_cast<float>(localWidth) * 0.5F, static_cast<float>(localHeight) * 0.5F};
  cv::Mat rotation = cv::getRotationMatrix2D(center, textFrame.transform.rotationDegrees, 1.0);
  const cv::Rect2f bounds = cv::RotatedRect{center, localMat.size(), static_cast<float>(textFrame.transform.rotationDegrees)}.boundingRect2f();
  rotation.at<double>(0, 2) += (static_cast<double>(bounds.width) * 0.5) - static_cast<double>(center.x);
  rotation.at<double>(1, 2) += (static_cast<double>(bounds.height) * 0.5) - static_cast<double>(center.y);

  const int rotatedWidth = std::max(1, static_cast<int>(std::ceil(bounds.width)));
  const int rotatedHeight = std::max(1, static_cast<int>(std::ceil(bounds.height)));
  RenderedImage rotated{
    foundation::Resolution{rotatedWidth, rotatedHeight},
    std::vector<std::uint8_t>(static_cast<std::size_t>(rotatedWidth * rotatedHeight * 4), 0)
  };
  cv::Mat rotatedMat{
    rotatedHeight,
    rotatedWidth,
    CV_8UC4,
    rotated.rgbaPixels.data()
  };
  cv::warpAffine(
    localMat,
    rotatedMat,
    rotation,
    rotatedMat.size(),
    cv::INTER_LINEAR,
    cv::BORDER_TRANSPARENT
  );
  return rotated;
}

void compositeTextFrames(RenderedImage& canvas, const std::vector<RenderedTextFrame>& textFrames) {
  for (const RenderedTextFrame& textFrame : textFrames) {
    if (textFrame.text.empty() || textFrame.transform.opacity <= 0.0) {
      continue;
    }

    RenderedImage textImage = rasterizeTextFrame(textFrame);
    const int centerX = static_cast<int>(std::lround(
      (static_cast<double>(canvas.resolution.width) * 0.5) +
      (textFrame.transform.position.x * static_cast<double>(canvas.resolution.width))
    ));
    const int centerY = static_cast<int>(std::lround(
      (static_cast<double>(canvas.resolution.height) * 0.5) -
      (textFrame.transform.position.y * static_cast<double>(canvas.resolution.height))
    ));
    compositeImageAt(
      canvas,
      textImage,
      centerX - (textImage.resolution.width / 2),
      centerY - (textImage.resolution.height / 2)
    );
  }
}

foundation::Result<std::optional<RenderedImage>> buildRenderedImage(
  const std::vector<RenderedMediaFrame>& mediaFrames,
  const std::vector<RenderedTextFrame>& textFrames,
  IRenderFrameSource* frameSource,
  std::optional<foundation::Resolution> outputResolution = std::nullopt
) {
  if (!mediaFrames.empty() && frameSource == nullptr) {
    return std::optional<RenderedImage>{};
  }
  if (mediaFrames.empty() && (textFrames.empty() || !outputResolution.has_value())) {
    return std::optional<RenderedImage>{};
  }

  std::optional<RenderedImage> canvas;
  for (const RenderedMediaFrame& mediaFrame : mediaFrames) {
    auto sourceFrame = frameSource->frameAt(SourceFrameRequest{
      mediaFrame.assetId,
      mediaFrame.sourceTime,
      outputResolution
    });
    if (!sourceFrame) {
      return sourceFrame.error();
    }

    auto image = renderedImageFromSourceFrame(std::move(sourceFrame.value()));
    if (!image) {
      return image.error();
    }

    if (!canvas.has_value()) {
      const foundation::Resolution canvasResolution = outputResolution.value_or(image.value().resolution);
      canvas = RenderedImage{
        canvasResolution,
        std::vector<std::uint8_t>(
          static_cast<std::size_t>(canvasResolution.width * canvasResolution.height * 4),
          0
        )
      };
    }

    RenderedImage tinted = tintedImage(std::move(image.value()), mediaFrame);
    RenderedImage exposed = exposedImage(std::move(tinted), mediaFrame);
    RenderedImage transformed = transformedMediaImage(exposed, mediaFrame, canvas->resolution);
    compositeImageOver(canvas.value(), transformed);
  }

  if (!canvas.has_value() && outputResolution.has_value()) {
    canvas = RenderedImage{
      outputResolution.value(),
      std::vector<std::uint8_t>(
        static_cast<std::size_t>(outputResolution->width * outputResolution->height * 4),
        0
      )
    };
  }
  if (canvas.has_value()) {
    compositeTextFrames(canvas.value(), textFrames);
  }

  return canvas;
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
    image.value(),
    image->resolution,
    transform,
    sourceOffsetPixels,
    foundation::Vec2{}
  );
}

foundation::Result<RenderFrameResult> renderSampleFrame(
  runtime::RuntimeSample sample,
  const runtime::PreparedRuntimePlan& prepared,
  IRenderFrameSource* frameSource,
  const RenderFrameRequest& request,
  std::optional<foundation::Resolution> outputResolution = std::nullopt
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
    request,
    request.outputResolution
  );
}

foundation::Result<RenderFrameResult> renderSampleFrame(
  runtime::RuntimeSample sample,
  const runtime::PreparedRuntimePlan& prepared,
  IRenderFrameSource* frameSource,
  const RenderFrameRequest& request,
  std::optional<foundation::Resolution> outputResolution
) {
  applyCameraTransformOutputs(
    sample,
    prepared.dependencyGraph.projectId,
    prepared.sourceRevision
  );

  const std::vector<RenderedMediaFrame> mediaFrames = buildMediaFrames(
    sample,
    prepared.dependencyGraph.projectId,
    prepared.sourceRevision
  );
  const std::vector<RenderedTextFrame> textFrames = buildTextFrames(sample);
  const std::vector<RenderedAudioClip> audioClips = buildAudioClips(sample);
  const std::vector<RenderedCamera> cameras = buildRenderedCameras(sample);
  auto image = buildRenderedImage(mediaFrames, textFrames, frameSource, outputResolution);
  if (!image) {
    return image.error();
  }
  std::optional<RenderedImage> transformedImage = applyCameraTransformToImage(std::move(image.value()), cameras);

  return RenderFrameResult{
    RenderFrame{
      prepared.sourceRevision,
      prepared.planHash,
      request.time,
      describeSample(sample),
      mediaFrames,
      textFrames,
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
      },
      request.outputResolution
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
