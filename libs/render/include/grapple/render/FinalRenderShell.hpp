#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/render/ExportSettings.hpp>
#include <grapple/render/LocalRenderCore.hpp>

#include <optional>
#include <vector>

namespace grapple::render {

struct FinalRenderRequest {
  ExportSettings settings;
  IRenderRangeSink* sink = nullptr;
};

struct FinalRenderResult {
  foundation::FilePath outputPath;
  std::size_t framesEvaluated = 0;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
};

struct FinalRenderShellState {
  LocalRenderCoreState core;
  std::optional<ExportSettings> lastSettings;
  std::optional<foundation::FilePath> lastOutputPath;
};

class FinalRenderShell {
public:
  explicit FinalRenderShell(LocalRenderCore& core);

  foundation::Result<FinalRenderResult> render(const FinalRenderRequest& request);
  [[nodiscard]] FinalRenderShellState state() const noexcept;

private:
  LocalRenderCore& core_;
  std::optional<ExportSettings> lastSettings_;
  std::optional<foundation::FilePath> lastOutputPath_;
};

} // namespace grapple::render
