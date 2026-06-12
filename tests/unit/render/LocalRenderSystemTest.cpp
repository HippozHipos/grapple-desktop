#include <grapple/render/LocalRenderSystem.hpp>

#include <TestAssert.hpp>

namespace {

grapple::projection::RenderPlan makeRenderPlan() {
  return grapple::projection::RenderPlan{
    grapple::foundation::ProjectId{"proj_render"},
    grapple::foundation::RevisionId{"rev_3"},
    grapple::projection::RenderStage{"Render Test"},
    grapple::foundation::TimeSeconds{12.0},
    {},
    {
      grapple::projection::RenderLayer{
        grapple::foundation::NodeId{"node_track"},
        "Video"
      }
    },
    {
      grapple::projection::RenderClip{
        grapple::foundation::NodeId{"node_clip"},
        grapple::foundation::NodeId{"node_track"},
        grapple::timeline::ClipPayload{
          grapple::timeline::ClipKind::Video,
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            grapple::foundation::TimeSeconds{6.0}
          },
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            grapple::foundation::TimeSeconds{6.0}
          },
          1.0,
          grapple::foundation::AssetId{"asset_video"},
          grapple::timeline::Transform{}
        }
      }
    },
    {},
    {},
    {}
  };
}

grapple::render::ExportSettings makeExportSettings(grapple::foundation::Resolution resolution) {
  return grapple::render::ExportSettings{
    grapple::foundation::TimeRange{
      grapple::foundation::TimeSeconds{0.0},
      grapple::foundation::TimeSeconds{1.0}
    },
    grapple::foundation::FrameRate{2, 1},
    resolution,
    grapple::render::Codec{"prores"},
    grapple::render::RenderQuality::Final,
    grapple::foundation::FilePath{"/exports/test.mov"}
  };
}

} // namespace

int main() {
  using namespace grapple;

  runtime::RuntimeEvaluator runtime;
  render::LocalRenderSystem renderSystem{runtime};

  const auto seekBeforeLoad = renderSystem.seek(foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(!seekBeforeLoad);
  GRAPPLE_REQUIRE(seekBeforeLoad.error().code == "render.plan_missing");

  const auto exportBeforeLoad = renderSystem.exportRange(render::ExportRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(!exportBeforeLoad);
  GRAPPLE_REQUIRE(exportBeforeLoad.error().code == "render.plan_missing");

  const projection::RenderPlan plan = makeRenderPlan();
  const auto load = renderSystem.loadPlan(plan);
  GRAPPLE_REQUIRE(load);

  const auto stateAfterLoad = renderSystem.state();
  GRAPPLE_REQUIRE(stateAfterLoad);
  GRAPPLE_REQUIRE(stateAfterLoad.value().hasPlan);
  GRAPPLE_REQUIRE(stateAfterLoad.value().revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stateAfterLoad.value().preparedPlanHash.has_value());
  GRAPPLE_REQUIRE(stateAfterLoad.value().playback == render::PlaybackState::Paused);

  const auto seek = renderSystem.seek(foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(seek);

  const auto play = renderSystem.play();
  GRAPPLE_REQUIRE(play);

  const auto stateAfterPlay = renderSystem.state();
  GRAPPLE_REQUIRE(stateAfterPlay);
  GRAPPLE_REQUIRE(stateAfterPlay.value().playhead == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(stateAfterPlay.value().playback == render::PlaybackState::Playing);

  const auto renderedActiveFrame = renderSystem.renderPlaybackFrame(render::PlaybackFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(renderedActiveFrame);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.time == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.description == "layers=1 clips=1 cameras=0");
  GRAPPLE_REQUIRE(renderedActiveFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(renderedActiveFrame.value().renderDiagnostics.empty());

  const auto renderedInactiveFrame = renderSystem.renderPlaybackFrame(render::PlaybackFrameRequest{
    foundation::TimeSeconds{8.0},
    render::RenderQuality::Final
  });
  GRAPPLE_REQUIRE(renderedInactiveFrame);
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.description == "layers=1 clips=0 cameras=0");

  const auto pause = renderSystem.pause();
  GRAPPLE_REQUIRE(pause);

  const auto stateAfterPause = renderSystem.state();
  GRAPPLE_REQUIRE(stateAfterPause);
  GRAPPLE_REQUIRE(stateAfterPause.value().playback == render::PlaybackState::Paused);

  const render::ExportSettings exportSettings = makeExportSettings(foundation::Resolution{3840, 2160});
  const auto exportResult = renderSystem.exportRange(render::ExportRequest{exportSettings});
  GRAPPLE_REQUIRE(exportResult);
  GRAPPLE_REQUIRE(exportResult.value().outputPath.value == "/exports/test.mov");
  GRAPPLE_REQUIRE(exportResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(exportResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(exportResult.value().renderDiagnostics.empty());

  const auto stateAfterExport = renderSystem.state();
  GRAPPLE_REQUIRE(stateAfterExport);
  GRAPPLE_REQUIRE(stateAfterExport.value().revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stateAfterExport.value().preparedPlanHash == stateAfterLoad.value().preparedPlanHash);
  GRAPPLE_REQUIRE(stateAfterExport.value().lastExportSettings.has_value());
  GRAPPLE_REQUIRE((stateAfterExport.value().lastExportSettings->resolution == foundation::Resolution{3840, 2160}));
  GRAPPLE_REQUIRE(stateAfterExport.value().lastExportOutputPath->value == "/exports/test.mov");

  const auto changedExportSettings = renderSystem.exportRange(render::ExportRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(changedExportSettings);

  const auto stateAfterSettingsChange = renderSystem.state();
  GRAPPLE_REQUIRE(stateAfterSettingsChange);
  GRAPPLE_REQUIRE(stateAfterSettingsChange.value().revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stateAfterSettingsChange.value().preparedPlanHash == stateAfterLoad.value().preparedPlanHash);
  GRAPPLE_REQUIRE((stateAfterSettingsChange.value().lastExportSettings->resolution == foundation::Resolution{1920, 1080}));

  return 0;
}
