#include <grapple/media/VideoDecoder.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace grapple::media {

namespace {

struct FormatContextDeleter {
  void operator()(AVFormatContext* context) const noexcept {
    if (context != nullptr) {
      avformat_close_input(&context);
    }
  }
};

struct CodecContextDeleter {
  void operator()(AVCodecContext* context) const noexcept {
    avcodec_free_context(&context);
  }
};

struct PacketDeleter {
  void operator()(AVPacket* packet) const noexcept {
    av_packet_free(&packet);
  }
};

struct FrameDeleter {
  void operator()(AVFrame* frame) const noexcept {
    av_frame_free(&frame);
  }
};

struct SwsContextDeleter {
  void operator()(SwsContext* context) const noexcept {
    sws_freeContext(context);
  }
};

struct AvImageBuffer {
  std::uint8_t* data[4]{};
  int lineSizes[4]{};

  ~AvImageBuffer() {
    av_freep(&data[0]);
  }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

foundation::Error ffmpegError(std::string code, std::string message) {
  return foundation::Error{std::move(code), std::move(message)};
}

struct OpenVideo {
  FormatContextPtr format;
  CodecContextPtr codec;
  int streamIndex = -1;
};

struct DecodedAvFrame {
  FramePtr frame;
  foundation::TimeSeconds time;
};

foundation::Result<OpenVideo> openVideo(const foundation::FilePath& path) {
  AVFormatContext* rawFormat = nullptr;
  if (avformat_open_input(&rawFormat, path.value.c_str(), nullptr, nullptr) < 0) {
    return ffmpegError("media.video_open_failed", "Could not open video source " + path.value + ".");
  }
  FormatContextPtr format{rawFormat};

  if (avformat_find_stream_info(format.get(), nullptr) < 0) {
    return ffmpegError("media.video_stream_info_failed", "Could not read video stream info for " + path.value + ".");
  }

  const int streamIndex = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (streamIndex < 0) {
    return ffmpegError("media.video_stream_missing", "Video source has no video stream: " + path.value + ".");
  }

  const AVCodecParameters* parameters = format->streams[streamIndex]->codecpar;
  const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
  if (decoder == nullptr) {
    return ffmpegError("media.video_decoder_missing", "No decoder is available for video source " + path.value + ".");
  }

  AVCodecContext* rawCodec = avcodec_alloc_context3(decoder);
  if (rawCodec == nullptr) {
    return ffmpegError("media.video_decoder_alloc_failed", "Could not allocate decoder for " + path.value + ".");
  }
  CodecContextPtr codec{rawCodec};

  if (avcodec_parameters_to_context(codec.get(), parameters) < 0) {
    return ffmpegError("media.video_decoder_params_failed", "Could not configure decoder for " + path.value + ".");
  }

  if (avcodec_open2(codec.get(), decoder, nullptr) < 0) {
    return ffmpegError("media.video_decoder_open_failed", "Could not open decoder for " + path.value + ".");
  }

  return OpenVideo{std::move(format), std::move(codec), streamIndex};
}

foundation::Resolution outputResolutionFor(
  const foundation::Resolution& sourceResolution,
  std::optional<foundation::Resolution> targetResolution
) {
  if (!targetResolution.has_value()) {
    return sourceResolution;
  }
  return foundation::Resolution{
    std::max(1, targetResolution->width),
    std::max(1, targetResolution->height)
  };
}

foundation::TimeSeconds decodedFrameTime(
  const OpenVideo& video,
  const AVFrame& frame,
  foundation::TimeSeconds requestedTime
) {
  if (frame.best_effort_timestamp == AV_NOPTS_VALUE) {
    return requestedTime;
  }

  const AVStream* stream = video.format->streams[video.streamIndex];
  return foundation::TimeSeconds{
    static_cast<double>(frame.best_effort_timestamp) * av_q2d(stream->time_base)
  };
}

foundation::Result<DecodedAvFrame> decodeFrameForward(OpenVideo& video, foundation::TimeSeconds time) {
  PacketPtr packet{av_packet_alloc()};
  FramePtr frame{av_frame_alloc()};
  if (!packet || !frame) {
    return ffmpegError("media.video_decode_alloc_failed", "Could not allocate video decode buffers.");
  }

  constexpr double TimestampEpsilon = 0.000001;
  while (true) {
    while (true) {
      const int receiveResult = avcodec_receive_frame(video.codec.get(), frame.get());
      if (receiveResult == 0) {
        const foundation::TimeSeconds frameTime = decodedFrameTime(video, *frame, time);
        if (frameTime.value + TimestampEpsilon >= time.value) {
          return DecodedAvFrame{std::move(frame), frameTime};
        }
        av_frame_unref(frame.get());
        continue;
      }
      if (receiveResult == AVERROR(EAGAIN)) {
        break;
      }
      if (receiveResult == AVERROR_EOF) {
        return ffmpegError("media.video_frame_decode_failed", "Could not decode requested video frame.");
      }
      return ffmpegError("media.video_frame_decode_failed", "Could not decode requested video frame.");
    }

    const int readResult = av_read_frame(video.format.get(), packet.get());
    if (readResult < 0) {
      return ffmpegError("media.video_frame_decode_failed", "Could not decode requested video frame.");
    }
    if (packet->stream_index == video.streamIndex) {
      const int sendResult = avcodec_send_packet(video.codec.get(), packet.get());
      av_packet_unref(packet.get());
      if (sendResult < 0) {
        return ffmpegError("media.video_frame_decode_failed", "Could not decode requested video frame.");
      }
      continue;
    }
    av_packet_unref(packet.get());
  }
}

foundation::Result<DecodedAvFrame> decodeFrameAfterSeek(OpenVideo& video, foundation::TimeSeconds time) {
  const AVStream* stream = video.format->streams[video.streamIndex];
  const std::int64_t timestamp = static_cast<std::int64_t>(
    std::llround(time.value / av_q2d(stream->time_base))
  );
  if (av_seek_frame(video.format.get(), video.streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
    return ffmpegError("media.video_seek_failed", "Could not seek requested video frame.");
  }
  avcodec_flush_buffers(video.codec.get());
  return decodeFrameForward(video, time);
}

foundation::FrameRate frameRateFor(const AVStream& stream) {
  AVRational rate = stream.avg_frame_rate.num != 0 && stream.avg_frame_rate.den != 0
    ? stream.avg_frame_rate
    : stream.r_frame_rate;
  if (rate.num <= 0 || rate.den <= 0) {
    return foundation::FrameRate{24, 1};
  }
  return foundation::FrameRate{rate.num, rate.den};
}

foundation::TimeSeconds durationFor(const AVFormatContext& format, const AVStream& stream) {
  if (stream.duration > 0) {
    return foundation::TimeSeconds{
      static_cast<double>(stream.duration) * av_q2d(stream.time_base)
    };
  }
  if (format.duration > 0) {
    return foundation::TimeSeconds{
      static_cast<double>(format.duration) / static_cast<double>(AV_TIME_BASE)
    };
  }
  return foundation::TimeSeconds{0.0};
}

foundation::Result<DecodedVideoFrame> rgbaFrameFromDecoded(
  const AVFrame& decoded,
  std::optional<foundation::Resolution> targetResolution
) {
  const foundation::Resolution sourceResolution{decoded.width, decoded.height};
  const foundation::Resolution outputResolution = outputResolutionFor(sourceResolution, targetResolution);
  AvImageBuffer scaledImage;
  if (av_image_alloc(
    scaledImage.data,
    scaledImage.lineSizes,
    outputResolution.width,
    outputResolution.height,
    AV_PIX_FMT_RGBA,
    32
  ) < 0) {
    return ffmpegError("media.video_scale_alloc_failed", "Could not allocate scaled video frame.");
  }

  SwsContextPtr scaler{sws_getContext(
    decoded.width,
    decoded.height,
    static_cast<AVPixelFormat>(decoded.format),
    outputResolution.width,
    outputResolution.height,
    AV_PIX_FMT_RGBA,
    SWS_AREA,
    nullptr,
    nullptr,
    nullptr
  )};
  if (!scaler) {
    return ffmpegError("media.video_scale_failed", "Could not scale decoded video frame.");
  }

  const int scaledRows = sws_scale(
    scaler.get(),
    decoded.data,
    decoded.linesize,
    0,
    decoded.height,
    scaledImage.data,
    scaledImage.lineSizes
  );
  if (scaledRows != outputResolution.height) {
    return ffmpegError("media.video_scale_failed", "Could not scale decoded video frame.");
  }

  std::vector<std::uint8_t> rgbaPixels(
    static_cast<std::size_t>(outputResolution.width) *
    static_cast<std::size_t>(outputResolution.height) *
    4U
  );
  const int packedLineSize = outputResolution.width * 4;
  for (int row = 0; row < outputResolution.height; ++row) {
    const std::uint8_t* source =
      scaledImage.data[0] + static_cast<std::size_t>(row) * static_cast<std::size_t>(scaledImage.lineSizes[0]);
    std::uint8_t* destination =
      rgbaPixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(packedLineSize);
    std::copy(source, source + packedLineSize, destination);
  }

  return DecodedVideoFrame{outputResolution, std::move(rgbaPixels)};
}

} // namespace

struct VideoDecodeSession::Impl {
  Impl(foundation::FilePath sourcePath, OpenVideo openVideo)
    : path{std::move(sourcePath)},
      video{std::move(openVideo)} {}

  foundation::FilePath path;
  OpenVideo video;
  std::optional<foundation::TimeSeconds> lastDecodedFrameTime;
};

VideoDecodeSession::VideoDecodeSession(std::unique_ptr<Impl> impl)
  : impl_{std::move(impl)} {}

VideoDecodeSession::~VideoDecodeSession() = default;

VideoDecodeSession::VideoDecodeSession(VideoDecodeSession&&) noexcept = default;

VideoDecodeSession& VideoDecodeSession::operator=(VideoDecodeSession&&) noexcept = default;

foundation::Result<VideoDecodeSession> VideoDecodeSession::open(const foundation::FilePath& path) {
  auto opened = openVideo(path);
  if (!opened) {
    return opened.error();
  }

  return VideoDecodeSession{
    std::make_unique<Impl>(path, std::move(opened.value()))
  };
}

foundation::Result<VideoMetadata> VideoDecodeSession::metadata() const {
  const AVStream* stream = impl_->video.format->streams[impl_->video.streamIndex];
  const int width = impl_->video.codec->width;
  const int height = impl_->video.codec->height;
  if (width <= 0 || height <= 0) {
    return ffmpegError("media.video_metadata_invalid", "Video file metadata is incomplete for " + impl_->path.value + ".");
  }

  const foundation::TimeSeconds duration = durationFor(*impl_->video.format, *stream);
  if (duration.value <= 0.0) {
    return ffmpegError("media.video_metadata_invalid", "Video file metadata is incomplete for " + impl_->path.value + ".");
  }

  return VideoMetadata{
    duration,
    foundation::Resolution{width, height},
    frameRateFor(*stream)
  };
}

foundation::Result<DecodedVideoFrame> VideoDecodeSession::frameAt(
  foundation::TimeSeconds time,
  std::optional<foundation::Resolution> targetResolution
) {
  constexpr double ForwardDecodeWindowSeconds = 2.0;
  constexpr double TimestampEpsilon = 0.000001;
  const bool canDecodeForward =
    impl_->lastDecodedFrameTime.has_value() &&
    time.value > impl_->lastDecodedFrameTime->value + TimestampEpsilon &&
    time.value - impl_->lastDecodedFrameTime->value <= ForwardDecodeWindowSeconds;

  auto decoded = canDecodeForward
    ? decodeFrameForward(impl_->video, time)
    : decodeFrameAfterSeek(impl_->video, time);
  if (!decoded) {
    return decoded.error();
  }

  impl_->lastDecodedFrameTime = decoded.value().time;
  return rgbaFrameFromDecoded(*decoded.value().frame, targetResolution);
}

foundation::Result<VideoMetadata> inspectVideoFile(const foundation::FilePath& path) {
  auto session = VideoDecodeSession::open(path);
  if (!session) {
    return session.error();
  }

  return session.value().metadata();
}

foundation::Result<DecodedVideoFrame> decodeVideoFrame(
  const foundation::FilePath& path,
  foundation::TimeSeconds time,
  std::optional<foundation::Resolution> targetResolution
) {
  auto session = VideoDecodeSession::open(path);
  if (!session) {
    return session.error();
  }

  return session.value().frameAt(time, targetResolution);
}

} // namespace grapple::media
