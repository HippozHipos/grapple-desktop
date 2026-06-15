#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/jobs/CancellationToken.hpp>
#include <grapple/jobs/ProgressSink.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/LocalRenderSystem.hpp>

namespace grapple::app {

class NativeExportSession {
public:
  explicit NativeExportSession(render::LocalRenderSystem& renderSystem);

  foundation::Result<render::FinalRenderResult> renderPlan(
    const projection::RenderPlan& plan,
    render::ExportSettings settings
  );
  foundation::Result<render::FinalRenderResult> renderPlanToVideo(
    const projection::RenderPlan& plan,
    render::ExportSettings settings,
    jobs::IProgressSink* progress = nullptr,
    jobs::CancellationToken* cancellation = nullptr
  );
  [[nodiscard]] render::FinalRenderShellState state() const;

private:
  render::LocalRenderSystem& renderSystem_;
};

} // namespace grapple::app
