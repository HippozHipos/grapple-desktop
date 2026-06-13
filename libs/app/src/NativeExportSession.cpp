#include <grapple/app/NativeExportSession.hpp>

#include <utility>

namespace grapple::app {

NativeExportSession::NativeExportSession(NativeProjectSession& project)
  : NativeExportSession{project, {}} {}

NativeExportSession::NativeExportSession(
  NativeProjectSession& project,
  std::vector<runtime::IEffectRuntime*> effectRuntimes
) : project_{project},
    runtime_{std::move(effectRuntimes)},
    core_{runtime_},
    final_{core_} {}

NativeExportSession::NativeExportSession(
  NativeProjectSession& project,
  render::IRenderFrameSource& frameSource,
  std::vector<runtime::IEffectRuntime*> effectRuntimes
) : project_{project},
    runtime_{std::move(effectRuntimes)},
    core_{runtime_, frameSource},
    final_{core_} {}

foundation::Result<NativeExportPrepareResult> NativeExportSession::prepareFromProject() {
  auto planResult = project_.buildRenderPlan();
  if (!planResult) {
    return planResult.error();
  }

  auto loadResult = core_.loadPlan(planResult.value().plan);
  if (!loadResult) {
    return loadResult.error();
  }

  const render::LocalRenderCoreState coreState = core_.state();
  return NativeExportPrepareResult{
    planResult.value().plan.revision,
    coreState.preparedPlanHash.value()
  };
}

foundation::Result<render::FinalRenderResult> NativeExportSession::render(render::ExportSettings settings) {
  return final_.render(render::FinalRenderRequest{std::move(settings)});
}

render::FinalRenderShellState NativeExportSession::state() const noexcept {
  return final_.state();
}

} // namespace grapple::app
