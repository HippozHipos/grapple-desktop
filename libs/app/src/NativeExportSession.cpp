#include <grapple/app/NativeExportSession.hpp>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace grapple::app {

namespace {

class NativeVideoExportSink final : public render::IRenderRangeSink {
public:
  NativeVideoExportSink(
    render::ExportSettings settings,
    jobs::IProgressSink* progress,
    jobs::CancellationToken* cancellation
  )
    : settings_{std::move(settings)},
      progress_{progress},
      cancellation_{cancellation},
      expectedFrames_{expectedFrameCount(settings_)} {}

  foundation::Result<void> writeFrame(std::size_t frameIndex, const render::RenderFrameResult& frame) override {
    (void)frameIndex;
    if (cancelled()) {
      return foundation::Error{"app.export_cancelled", "Video export was cancelled."};
    }
    if (!frame.frame.image.has_value()) {
      return foundation::Error{
        "app.export_frame_image_missing",
        "Video export requires rendered image frames."
      };
    }

    const render::RenderedImage& image = *frame.frame.image;
    const std::size_t expectedBytes =
      static_cast<std::size_t>(image.resolution.width) *
      static_cast<std::size_t>(image.resolution.height) *
      4;
    if (image.rgbaPixels.size() != expectedBytes) {
      return foundation::Error{
        "app.export_frame_image_invalid",
        "Rendered image buffer size does not match its resolution."
      };
    }

    auto opened = ensureOpen();
    if (!opened) {
      return opened.error();
    }

    cv::Mat rgba{
      image.resolution.height,
      image.resolution.width,
      CV_8UC4,
      const_cast<std::uint8_t*>(image.rgbaPixels.data())
    };
    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    if (image.resolution != settings_.resolution) {
      cv::Mat resized;
      cv::resize(bgr, resized, cv::Size{settings_.resolution.width, settings_.resolution.height});
      writer_.write(resized);
    } else {
      writer_.write(bgr);
    }
    ++framesWritten_;
    reportProgress();
    return {};
  }

  foundation::Result<void> close() {
    if (writer_.isOpened()) {
      writer_.release();
    }
    if (framesWritten_ == 0) {
      return foundation::Error{
        "app.export_no_frames_written",
        "Video export did not write any frames."
      };
    }
    return {};
  }

private:
  [[nodiscard]] bool cancelled() const noexcept {
    return cancellation_ != nullptr && cancellation_->cancelled();
  }

  static std::size_t expectedFrameCount(const render::ExportSettings& settings) {
    const double frames = settings.range.duration() * settings.frameRate.framesPerSecond();
    if (frames <= 0.0) {
      return 0;
    }
    return static_cast<std::size_t>(frames);
  }

  void reportProgress() {
    if (progress_ == nullptr || expectedFrames_ == 0) {
      return;
    }
    progress_->reportProgress(static_cast<double>(framesWritten_) / static_cast<double>(expectedFrames_));
  }

  foundation::Result<void> ensureOpen() {
    if (writer_.isOpened()) {
      return {};
    }

    const std::filesystem::path outputPath{settings_.outputPath.value};
    if (outputPath.has_parent_path()) {
      std::filesystem::create_directories(outputPath.parent_path());
    }

    const int fourcc = fourccForCodec(settings_.codec.name);
    if (fourcc == 0) {
      return foundation::Error{
        "app.export_codec_unsupported",
        "Video export codec " + settings_.codec.name + " is not supported by the native writer."
      };
    }

    writer_.open(
      settings_.outputPath.value,
      fourcc,
      settings_.frameRate.framesPerSecond(),
      cv::Size{settings_.resolution.width, settings_.resolution.height},
      true
    );
    if (!writer_.isOpened()) {
      return foundation::Error{
        "app.export_video_open_failed",
        "Could not open video export path " + settings_.outputPath.value + "."
      };
    }
    return {};
  }

  static int fourccForCodec(const std::string& codec) {
    if (codec == "mjpeg") {
      return cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    }
    if (codec == "mp4v") {
      return cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    }
    return 0;
  }

  render::ExportSettings settings_;
  jobs::IProgressSink* progress_ = nullptr;
  jobs::CancellationToken* cancellation_ = nullptr;
  std::size_t expectedFrames_ = 0;
  cv::VideoWriter writer_;
  std::size_t framesWritten_ = 0;
};

} // namespace

NativeExportSession::NativeExportSession(
  NativeProjectSession& project,
  render::LocalRenderSystem& renderSystem
) : project_{project},
    renderSystem_{renderSystem} {}

foundation::Result<NativeExportPrepareResult> NativeExportSession::prepareFromProject() {
  auto planResult = project_.buildRenderPlan();
  if (!planResult) {
    return planResult.error();
  }

  auto loadResult = renderSystem_.loadPlan(planResult.value().plan);
  if (!loadResult) {
    return loadResult.error();
  }

  const render::LocalRenderSystemState renderState = renderSystem_.state();
  return NativeExportPrepareResult{
    planResult.value().plan.revision,
    renderState.core.preparedPlanHash.value()
  };
}

foundation::Result<render::FinalRenderResult> NativeExportSession::render(render::ExportSettings settings) {
  return renderSystem_.exportRange(render::ExportRequest{std::move(settings)});
}

foundation::Result<render::FinalRenderResult> NativeExportSession::renderToVideo(
  render::ExportSettings settings,
  jobs::IProgressSink* progress,
  jobs::CancellationToken* cancellation
) {
  NativeVideoExportSink sink{settings, progress, cancellation};
  auto result = renderSystem_.exportRange(render::ExportRequest{std::move(settings), &sink});
  if (!result) {
    return result.error();
  }
  auto close = sink.close();
  if (!close) {
    return close.error();
  }
  return result;
}

foundation::Result<render::FinalRenderResult> NativeExportSession::renderPlan(
  projection::RenderPlan plan,
  render::ExportSettings settings
) {
  return renderSystem_.exportPlanRange(render::ExportPlanRequest{
    std::move(plan),
    std::move(settings)
  });
}

foundation::Result<render::FinalRenderResult> NativeExportSession::renderPlanToVideo(
  projection::RenderPlan plan,
  render::ExportSettings settings,
  jobs::IProgressSink* progress,
  jobs::CancellationToken* cancellation
) {
  NativeVideoExportSink sink{settings, progress, cancellation};
  auto result = renderSystem_.exportPlanRange(render::ExportPlanRequest{
    std::move(plan),
    std::move(settings),
    &sink
  });
  if (!result) {
    return result.error();
  }
  auto close = sink.close();
  if (!close) {
    return close.error();
  }
  return result;
}

render::FinalRenderShellState NativeExportSession::state() const {
  const render::LocalRenderSystemState renderState = renderSystem_.state();
  return render::FinalRenderShellState{
    renderState.core,
    renderState.lastExportSettings,
    renderState.lastExportOutputPath
  };
}

} // namespace grapple::app
