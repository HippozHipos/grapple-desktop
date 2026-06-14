#pragma once

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/LocalRenderSystem.hpp>

namespace grapple::app {

struct NativeExportPrepareResult {
  foundation::RevisionId revision;
  foundation::Hash256 preparedPlanHash;
};

class NativeExportSession {
public:
  NativeExportSession(NativeProjectSession& project, render::LocalRenderSystem& renderSystem);

  foundation::Result<NativeExportPrepareResult> prepareFromProject();
  foundation::Result<render::FinalRenderResult> render(render::ExportSettings settings);
  foundation::Result<render::FinalRenderResult> renderToVideo(render::ExportSettings settings);
  foundation::Result<render::FinalRenderResult> renderPlan(
    projection::RenderPlan plan,
    render::ExportSettings settings
  );
  foundation::Result<render::FinalRenderResult> renderPlanToVideo(
    projection::RenderPlan plan,
    render::ExportSettings settings
  );
  [[nodiscard]] render::FinalRenderShellState state() const;

private:
  NativeProjectSession& project_;
  render::LocalRenderSystem& renderSystem_;
};

} // namespace grapple::app
