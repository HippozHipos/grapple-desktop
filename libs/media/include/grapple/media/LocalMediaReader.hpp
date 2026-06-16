#pragma once

#include <grapple/media/MediaReader.hpp>
#include <grapple/media/MediaSource.hpp>

#include <memory>
#include <optional>

namespace grapple::media {

class LocalMediaReader final : public IMediaReader {
public:
  explicit LocalMediaReader(const MediaSourceCatalog& sources);
  ~LocalMediaReader() override;

  LocalMediaReader(const LocalMediaReader&) = delete;
  LocalMediaReader& operator=(const LocalMediaReader&) = delete;
  LocalMediaReader(LocalMediaReader&&) noexcept;
  LocalMediaReader& operator=(LocalMediaReader&&) noexcept;

  foundation::Result<MediaFrame> frameAt(
    foundation::AssetId assetId,
    foundation::TimeSeconds time,
    std::optional<foundation::Resolution> targetResolution = std::nullopt
  ) override;

  foundation::Result<AudioBuffer> audioRange(
    foundation::AssetId assetId,
    foundation::TimeRange range
  ) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace grapple::media
