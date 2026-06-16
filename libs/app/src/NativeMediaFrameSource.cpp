#include <grapple/app/NativeMediaFrameSource.hpp>

namespace grapple::app {

NativeMediaFrameSource::NativeMediaFrameSource(media::IMediaReader& reader)
  : reader_{reader} {}

foundation::Result<render::SourceFrame> NativeMediaFrameSource::frameAt(const render::SourceFrameRequest& request) {
  auto frame = reader_.frameAt(
    request.assetId,
    request.sourceTime,
    request.targetResolution
  );
  if (!frame) {
    return frame.error();
  }

  return render::SourceFrame{
    frame.value().assetId,
    frame.value().time,
    frame.value().resolution,
    std::move(frame.value().rgbaPixels)
  };
}

} // namespace grapple::app
