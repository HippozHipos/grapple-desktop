#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/render/LocalRenderCore.hpp>

namespace grapple::render {

enum class PreviewPlaybackState {
  Paused,
  Playing
};

struct PreviewRenderShellState {
  PreviewPlaybackState playback = PreviewPlaybackState::Paused;
  foundation::TimeSeconds playhead;
  LocalRenderCoreState core;
};

class PreviewRenderShell {
public:
  explicit PreviewRenderShell(LocalRenderCore& core);

  foundation::Result<void> seek(foundation::TimeSeconds time);
  foundation::Result<void> play();
  foundation::Result<void> pause();
  foundation::Result<RenderFrameResult> renderFrame(const RenderFrameRequest& request) const;
  [[nodiscard]] PreviewRenderShellState state() const noexcept;

private:
  LocalRenderCore& core_;
  PreviewPlaybackState playback_ = PreviewPlaybackState::Paused;
  foundation::TimeSeconds playhead_;
};

} // namespace grapple::render
