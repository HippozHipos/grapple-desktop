#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/ExportRenderer.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <optional>

namespace grapple::render {

struct ExportSessionState {
  bool hasExport = false;
  std::optional<foundation::RevisionId> revision;
  std::optional<foundation::Hash256> preparedPlanHash;
  std::optional<ExportSettings> settings;
};

class ExportSession {
public:
  explicit ExportSession(runtime::RuntimeEvaluator& runtime);

  foundation::Result<void> start(const ExportRequest& request);
  [[nodiscard]] foundation::Result<ExportSessionState> state() const;

private:
  runtime::RuntimeEvaluator& runtime_;
  ExportSessionState state_;
};

} // namespace grapple::render
