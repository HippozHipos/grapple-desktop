#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/PreviewRenderer.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <optional>

namespace grapple::render {

struct PreviewState {
  bool hasPlan = false;
  PlaybackState playback = PlaybackState::Paused;
  foundation::TimeSeconds playhead;
  std::optional<foundation::RevisionId> revision;
  std::optional<foundation::Hash256> preparedPlanHash;
};

class PreviewSession {
public:
  explicit PreviewSession(runtime::RuntimeEvaluator& runtime);

  foundation::Result<void> loadPlan(const projection::RenderPlan& plan);
  foundation::Result<void> seek(foundation::TimeSeconds time);
  foundation::Result<void> play();
  foundation::Result<void> pause();
  foundation::Result<PreviewRenderFrameResult> renderFrame(const PreviewRenderFrameRequest& request) const;
  [[nodiscard]] foundation::Result<PreviewState> state() const;

private:
  runtime::RuntimeEvaluator& runtime_;
  std::optional<runtime::PreparedRuntimePlan> prepared_;
  PreviewState state_;
};

} // namespace grapple::render
