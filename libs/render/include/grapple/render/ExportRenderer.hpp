#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/ExportSettings.hpp>
#include <grapple/render/RenderDiagnostic.hpp>

#include <vector>

namespace grapple::render {

struct ExportRequest {
  projection::RenderPlan plan;
  ExportSettings settings;
};

struct ExportResult {
  foundation::FilePath outputPath;
  std::vector<RenderDiagnostic> diagnostics;
};

class IExportProgressSink {
public:
  virtual ~IExportProgressSink() = default;

  virtual void reportProgress(double progress) = 0;
};

class IExportRenderer {
public:
  virtual ~IExportRenderer() = default;

  virtual foundation::Result<ExportResult> render(
    const ExportRequest& request,
    IExportProgressSink& progress
  ) = 0;
};

} // namespace grapple::render

