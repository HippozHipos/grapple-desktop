#pragma once

#include <grapple/media/MediaReader.hpp>
#include <grapple/render/RenderFrame.hpp>

namespace grapple::app {

class NativeMediaFrameSource final : public render::IRenderFrameSource {
public:
  explicit NativeMediaFrameSource(media::IMediaReader& reader);

  foundation::Result<render::SourceFrame> frameAt(const render::SourceFrameRequest& request) override;

private:
  media::IMediaReader& reader_;
};

} // namespace grapple::app
