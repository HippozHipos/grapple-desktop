#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/projection/RenderPlan.hpp>
#include <grapple/render/ExportSettings.hpp>
#include <grapple/render/RenderDiagnostic.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>

#include <optional>
#include <string>
#include <vector>

namespace grapple::render {

enum class RenderQuality {
  Draft,
  Final
};

enum class PlaybackState {
  Paused,
  Playing
};

struct PlaybackFrame {
  foundation::TimeSeconds time;
  std::string description;
};

struct PlaybackFrameRequest {
  foundation::TimeSeconds time;
  RenderQuality quality = RenderQuality::Draft;
};

struct PlaybackFrameResult {
  PlaybackFrame frame;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
};

struct ExportRequest {
  ExportSettings settings;
};

struct ExportResult {
  foundation::FilePath outputPath;
  std::size_t framesEvaluated = 0;
  std::vector<runtime::RuntimeDiagnostic> runtimeDiagnostics;
  std::vector<RenderDiagnostic> renderDiagnostics;
};

struct LocalRenderSystemState {
  bool hasPlan = false;
  PlaybackState playback = PlaybackState::Paused;
  foundation::TimeSeconds playhead;
  std::optional<foundation::RevisionId> revision;
  std::optional<foundation::Hash256> preparedPlanHash;
  std::optional<ExportSettings> lastExportSettings;
  std::optional<foundation::FilePath> lastExportOutputPath;
};

class LocalRenderSystem {
public:
  explicit LocalRenderSystem(runtime::RuntimeEvaluator& runtime);

  foundation::Result<void> loadPlan(const projection::RenderPlan& plan);
  foundation::Result<void> seek(foundation::TimeSeconds time);
  foundation::Result<void> play();
  foundation::Result<void> pause();
  foundation::Result<PlaybackFrameResult> renderPlaybackFrame(const PlaybackFrameRequest& request) const;
  foundation::Result<ExportResult> exportRange(const ExportRequest& request);
  [[nodiscard]] foundation::Result<LocalRenderSystemState> state() const;

private:
  runtime::RuntimeEvaluator& runtime_;
  std::optional<runtime::PreparedRuntimePlan> prepared_;
  LocalRenderSystemState state_;
};

} // namespace grapple::render
