#pragma once

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/render/FinalRenderShell.hpp>
#include <grapple/render/LocalRenderCore.hpp>

namespace grapple::app {

struct NativeExportPrepareResult {
  foundation::RevisionId revision;
  foundation::Hash256 preparedPlanHash;
};

class NativeExportSession {
public:
  NativeExportSession(NativeProjectSession& project, render::LocalRenderCore& core);

  foundation::Result<NativeExportPrepareResult> prepareFromProject();
  foundation::Result<render::FinalRenderResult> render(render::ExportSettings settings);
  foundation::Result<render::FinalRenderResult> renderToVideo(render::ExportSettings settings);
  [[nodiscard]] render::FinalRenderShellState state() const noexcept;

private:
  NativeProjectSession& project_;
  render::LocalRenderCore& core_;
  render::FinalRenderShell final_;
};

} // namespace grapple::app
