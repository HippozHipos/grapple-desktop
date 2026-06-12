#include <grapple/render/FinalRenderShell.hpp>
#include <grapple/render/PreviewRenderShell.hpp>

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
      },
      grapple::projection::RenderClip{
        grapple::foundation::NodeId{"node_audio_clip"},
        grapple::foundation::NodeId{"node_track"},
        grapple::timeline::ClipPayload{
          grapple::timeline::ClipKind::Audio,
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            grapple::foundation::TimeSeconds{6.0}
          },
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{10.0},
            grapple::foundation::TimeSeconds{16.0}
          },
          1.0,
          grapple::foundation::AssetId{"asset_audio"},
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
  render::LocalRenderCore core{runtime};
  render::PreviewRenderShell preview{core};
  render::FinalRenderShell finalRender{core};

  const auto seekBeforeLoad = preview.seek(foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(!seekBeforeLoad);
  GRAPPLE_REQUIRE(seekBeforeLoad.error().code == "render.plan_missing");

  const auto finalBeforeLoad = finalRender.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(!finalBeforeLoad);
  GRAPPLE_REQUIRE(finalBeforeLoad.error().code == "render.plan_missing");

  const projection::RenderPlan plan = makeRenderPlan();
  const auto load = core.loadPlan(plan);
  GRAPPLE_REQUIRE(load);

  const auto coreAfterLoad = core.state();
  GRAPPLE_REQUIRE(coreAfterLoad.hasPlan);
  GRAPPLE_REQUIRE(coreAfterLoad.revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(coreAfterLoad.preparedPlanHash.has_value());

  const auto previewAfterLoad = preview.state();
  GRAPPLE_REQUIRE(previewAfterLoad.core.preparedPlanHash == coreAfterLoad.preparedPlanHash);
  GRAPPLE_REQUIRE(previewAfterLoad.playback == render::PreviewPlaybackState::Paused);

  const auto seek = preview.seek(foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(seek);

  const auto play = preview.play();
  GRAPPLE_REQUIRE(play);

  const auto stateAfterPlay = preview.state();
  GRAPPLE_REQUIRE(stateAfterPlay.playhead == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(stateAfterPlay.playback == render::PreviewPlaybackState::Playing);
  GRAPPLE_REQUIRE(stateAfterPlay.core.preparedPlanHash == coreAfterLoad.preparedPlanHash);

  const auto renderedActiveFrame = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(renderedActiveFrame);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.time == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.description == "layers=1 clips=2 cameras=0 effects=0");
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames.size() == 1);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].clipNodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].trackNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].kind == render::RenderedMediaKind::Video);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].sourceTime == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(renderedActiveFrame.value().renderDiagnostics.empty());

  const auto renderedInactiveFrame = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{8.0},
    render::RenderQuality::Final
  });
  GRAPPLE_REQUIRE(renderedInactiveFrame);
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.description == "layers=1 clips=0 cameras=0 effects=0");
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.mediaFrames.empty());

  const auto pause = preview.pause();
  GRAPPLE_REQUIRE(pause);

  const auto stateAfterPause = preview.state();
  GRAPPLE_REQUIRE(stateAfterPause.playback == render::PreviewPlaybackState::Paused);

  const render::ExportSettings exportSettings = makeExportSettings(foundation::Resolution{3840, 2160});
  const auto finalResult = finalRender.render(render::FinalRenderRequest{exportSettings});
  GRAPPLE_REQUIRE(finalResult);
  GRAPPLE_REQUIRE(finalResult.value().outputPath.value == "/exports/test.mov");
  GRAPPLE_REQUIRE(finalResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(finalResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(finalResult.value().renderDiagnostics.empty());

  const auto finalState = finalRender.state();
  GRAPPLE_REQUIRE(finalState.core.preparedPlanHash == coreAfterLoad.preparedPlanHash);
  GRAPPLE_REQUIRE(finalState.lastSettings.has_value());
  GRAPPLE_REQUIRE((finalState.lastSettings->resolution == foundation::Resolution{3840, 2160}));
  GRAPPLE_REQUIRE(finalState.lastOutputPath->value == "/exports/test.mov");

  const auto changedFinalSettings = finalRender.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(changedFinalSettings);

  const auto stateAfterSettingsChange = finalRender.state();
  GRAPPLE_REQUIRE(stateAfterSettingsChange.core.preparedPlanHash == coreAfterLoad.preparedPlanHash);
  GRAPPLE_REQUIRE((stateAfterSettingsChange.lastSettings->resolution == foundation::Resolution{1920, 1080}));

  return 0;
}
