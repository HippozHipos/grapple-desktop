#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/RenderDiagnostic.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>

#include <string>
#include <vector>

namespace grapple::render {

enum class PreviewQuality {
  Draft,
  Full
};

enum class PlaybackState {
  Paused,
  Playing
};

struct PreviewSettings {
  PreviewQuality quality = PreviewQuality::Draft;
};

struct PreviewFrame {
  foundation::TimeSeconds time;
  std::string description;
};

struct PreviewLoadRequest {
  projection::RenderPlan plan;
  PreviewSettings settings;
};

struct PreviewRenderFrameRequest {
  foundation::TimeSeconds time;
  PreviewQuality quality = PreviewQuality::Draft;
};

struct PreviewRenderFrameResult {
  PreviewFrame frame;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
};

class IPreviewRenderer {
public:
  virtual ~IPreviewRenderer() = default;

  virtual foundation::Result<void> load(const PreviewLoadRequest& request) = 0;
  virtual foundation::Result<void> setPlayhead(foundation::TimeSeconds time) = 0;
  virtual foundation::Result<void> setPlaybackState(PlaybackState state) = 0;
  virtual foundation::Result<PreviewRenderFrameResult> renderFrame(
    const PreviewRenderFrameRequest& request
  ) = 0;
};

} // namespace grapple::render

