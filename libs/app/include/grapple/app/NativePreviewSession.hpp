#pragma once

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/render/LocalRenderCore.hpp>
#include <grapple/render/PreviewRenderShell.hpp>

namespace grapple::app {

struct NativePreviewRefreshResult {
  foundation::RevisionId revision;
  foundation::Hash256 preparedPlanHash;
};

class NativePreviewSession {
public:
  NativePreviewSession(NativeProjectSession& project, render::LocalRenderCore& core);

  foundation::Result<NativePreviewRefreshResult> refreshFromProject();
  foundation::Result<void> seek(foundation::TimeSeconds time);
  foundation::Result<void> play();
  foundation::Result<void> pause();
  foundation::Result<render::RenderFrameResult> renderFrame(render::RenderFrameRequest request) const;
  [[nodiscard]] render::PreviewRenderShellState state() const noexcept;

private:
  NativeProjectSession& project_;
  render::LocalRenderCore& core_;
  render::PreviewRenderShell preview_;
};

} // namespace grapple::app
