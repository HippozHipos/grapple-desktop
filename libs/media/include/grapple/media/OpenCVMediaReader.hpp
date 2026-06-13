#pragma once

#include <grapple/media/MediaReader.hpp>
#include <grapple/media/MediaSource.hpp>

#include <memory>

namespace grapple::media {

class OpenCVMediaReader final : public IMediaReader {
public:
  explicit OpenCVMediaReader(const MediaSourceCatalog& sources);
  ~OpenCVMediaReader() override;

  OpenCVMediaReader(const OpenCVMediaReader&) = delete;
  OpenCVMediaReader& operator=(const OpenCVMediaReader&) = delete;
  OpenCVMediaReader(OpenCVMediaReader&&) noexcept;
  OpenCVMediaReader& operator=(OpenCVMediaReader&&) noexcept;

  foundation::Result<MediaFrame> frameAt(
    foundation::AssetId assetId,
    foundation::TimeSeconds time,
    MediaQuality quality
  ) override;

  foundation::Result<AudioBuffer> audioRange(
    foundation::AssetId assetId,
    foundation::TimeRange range,
    MediaQuality quality
  ) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace grapple::media
