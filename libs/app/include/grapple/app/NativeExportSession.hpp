#pragma once

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/render/FinalRenderShell.hpp>
#include <grapple/render/LocalRenderCore.hpp>
#include <grapple/runtime/EffectRuntime.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <vector>

namespace grapple::app {

struct NativeExportPrepareResult {
  foundation::RevisionId revision;
  foundation::Hash256 preparedPlanHash;
};

class NativeExportSession {
public:
  explicit NativeExportSession(NativeProjectSession& project);
  NativeExportSession(NativeProjectSession& project, std::vector<runtime::IEffectRuntime*> effectRuntimes);

  foundation::Result<NativeExportPrepareResult> prepareFromProject();
  foundation::Result<render::FinalRenderResult> render(render::ExportSettings settings);
  [[nodiscard]] render::FinalRenderShellState state() const noexcept;

private:
  NativeProjectSession& project_;
  runtime::RuntimeEvaluator runtime_;
  render::LocalRenderCore core_;
  render::FinalRenderShell final_;
};

} // namespace grapple::app
