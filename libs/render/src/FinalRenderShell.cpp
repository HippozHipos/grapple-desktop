#include <grapple/render/FinalRenderShell.hpp>

#include <utility>

namespace grapple::render {

FinalRenderShell::FinalRenderShell(LocalRenderCore& core)
  : core_{core} {}

foundation::Result<FinalRenderResult> FinalRenderShell::render(const FinalRenderRequest& request) {
  auto range = core_.renderRange(RenderRangeRequest{
    request.settings.range,
    request.settings.frameRate,
    request.settings.quality,
    request.sink
  });
  if (!range) {
    return range.error();
  }

  lastSettings_ = request.settings;
  lastOutputPath_ = request.settings.outputPath;
  return FinalRenderResult{
    request.settings.outputPath,
    range.value().sourceRevision,
    range.value().renderPlanHash,
    range.value().framesEvaluated,
    range.value().runtimeDiagnostics,
    range.value().renderDiagnostics
  };
}

FinalRenderShellState FinalRenderShell::state() const noexcept {
  return FinalRenderShellState{
    core_.state(),
    lastSettings_,
    lastOutputPath_
  };
}

} // namespace grapple::render
