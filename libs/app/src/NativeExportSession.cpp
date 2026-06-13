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
  explicit NativeVideoExportSink(render::ExportSettings settings)
    : settings_{std::move(settings)} {}

  foundation::Result<void> writeFrame(std::size_t frameIndex, const render::RenderFrameResult& frame) override {
    (void)frameIndex;
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
  cv::VideoWriter writer_;
  std::size_t framesWritten_ = 0;
};

} // namespace

NativeExportSession::NativeExportSession(
  NativeProjectSession& project,
  render::LocalRenderCore& core
) : project_{project},
    core_{core},
    final_{core_} {}

foundation::Result<NativeExportPrepareResult> NativeExportSession::prepareFromProject() {
  auto planResult = project_.buildRenderPlan();
  if (!planResult) {
    return planResult.error();
  }

  auto loadResult = core_.loadPlan(planResult.value().plan);
  if (!loadResult) {
    return loadResult.error();
  }

  const render::LocalRenderCoreState coreState = core_.state();
  return NativeExportPrepareResult{
    planResult.value().plan.revision,
    coreState.preparedPlanHash.value()
  };
}

foundation::Result<render::FinalRenderResult> NativeExportSession::render(render::ExportSettings settings) {
  return final_.render(render::FinalRenderRequest{std::move(settings)});
}

foundation::Result<render::FinalRenderResult> NativeExportSession::renderToVideo(render::ExportSettings settings) {
  NativeVideoExportSink sink{settings};
  auto result = final_.render(render::FinalRenderRequest{std::move(settings), &sink});
  if (!result) {
    return result.error();
  }
  auto close = sink.close();
  if (!close) {
    return close.error();
  }
  return result;
}

render::FinalRenderShellState NativeExportSession::state() const noexcept {
  return final_.state();
}

} // namespace grapple::app
