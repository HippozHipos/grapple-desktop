#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/RenderFrame.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <optional>

namespace grapple::render {

struct LocalRenderCoreState {
  bool hasPlan = false;
  std::optional<foundation::RevisionId> revision;
  std::optional<foundation::Hash256> preparedPlanHash;
};

class LocalRenderCore {
public:
  explicit LocalRenderCore(runtime::RuntimeEvaluator& runtime);
  LocalRenderCore(runtime::RuntimeEvaluator& runtime, IRenderFrameSource& frameSource);

  foundation::Result<void> loadPlan(const projection::RenderPlan& plan);
  foundation::Result<RenderFrameResult> renderFrame(const RenderFrameRequest& request) const;
  foundation::Result<RenderRangeResult> renderRange(const RenderRangeRequest& request) const;
  [[nodiscard]] LocalRenderCoreState state() const noexcept;

private:
  runtime::RuntimeEvaluator& runtime_;
  IRenderFrameSource* frameSource_ = nullptr;
  std::optional<runtime::PreparedRuntimePlan> prepared_;
  LocalRenderCoreState state_;
};

} // namespace grapple::render
