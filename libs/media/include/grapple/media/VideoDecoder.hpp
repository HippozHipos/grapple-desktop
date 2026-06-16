#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace grapple::media {

struct VideoMetadata {
  foundation::TimeSeconds duration;
  foundation::Resolution resolution;
  foundation::FrameRate frameRate;
};

struct DecodedVideoFrame {
  foundation::Resolution resolution;
  std::vector<std::uint8_t> rgbaPixels;
};

class VideoDecodeSession final {
public:
  ~VideoDecodeSession();

  VideoDecodeSession(const VideoDecodeSession&) = delete;
  VideoDecodeSession& operator=(const VideoDecodeSession&) = delete;
  VideoDecodeSession(VideoDecodeSession&&) noexcept;
  VideoDecodeSession& operator=(VideoDecodeSession&&) noexcept;

  static foundation::Result<VideoDecodeSession> open(const foundation::FilePath& path);

  [[nodiscard]] foundation::Result<VideoMetadata> metadata() const;
  foundation::Result<DecodedVideoFrame> frameAt(
    foundation::TimeSeconds time,
    std::optional<foundation::Resolution> targetResolution = std::nullopt
  );

private:
  struct Impl;

  explicit VideoDecodeSession(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

foundation::Result<VideoMetadata> inspectVideoFile(const foundation::FilePath& path);

foundation::Result<DecodedVideoFrame> decodeVideoFrame(
  const foundation::FilePath& path,
  foundation::TimeSeconds time,
  std::optional<foundation::Resolution> targetResolution = std::nullopt
);

} // namespace grapple::media
