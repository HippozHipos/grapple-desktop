#include <grapple/render/FinalRenderShell.hpp>
#include <grapple/render/LocalRenderSystem.hpp>
#include <grapple/render/PreviewRenderShell.hpp>
#include <grapple/runtime/EffectRuntime.hpp>
#include <grapple/runtime/RuntimeOutputNames.hpp>

#include <TestAssert.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace {

class CameraTransformRuntime final : public grapple::runtime::IEffectRuntime {
public:
  explicit CameraTransformRuntime(bool emitInvalidTransform = false)
    : emitInvalidTransform_{emitInvalidTransform} {}

  bool supports(const grapple::projection::RenderEffectNode& node) const override {
    return node.payload.implementation.kind == grapple::timeline::EffectImplementationKind::Python;
  }

  grapple::foundation::Result<grapple::runtime::EffectPrepareResult> prepare(
    const grapple::runtime::EffectPrepareRequest& request
  ) override {
    ++prepareCount;
    return grapple::runtime::EffectPrepareResult{
      grapple::runtime::PreparedEffectNode{
        request.graph.id,
        request.graph.targetNodeId,
        request.node.sourceNodeId,
        request.node.payload.activeRange,
        nullptr,
        grapple::runtime::RuntimeParamSet{},
        {}
      },
      {}
    };
  }

  grapple::foundation::Result<grapple::runtime::EffectProcessResult> process(
    const grapple::runtime::EffectProcessRequest& request
  ) override {
    ++processCount;
    grapple::runtime::RuntimeValue transformValue = grapple::timeline::Transform2D{
      grapple::foundation::Vec2{request.time.value, request.time.value * 2.0},
      grapple::foundation::Vec2{1.0, 1.0},
      request.time.value * 10.0,
      1.0
    };
    if (emitInvalidTransform_) {
      transformValue = request.time.value;
    }

    return grapple::runtime::EffectProcessResult{
      grapple::runtime::RuntimeEffectOutput{
        request.prepared.effectGraphId,
        request.prepared.targetNodeId,
        request.prepared.sourceNodeId,
        {
          grapple::runtime::RuntimeNamedValue{
            grapple::runtime::output_name::CameraTransform,
            std::move(transformValue)
          }
        }
      },
      {}
    };
  }

  int prepareCount = 0;
  int processCount = 0;

private:
  bool emitInvalidTransform_ = false;
};

class TestFrameSource final : public grapple::render::IRenderFrameSource {
public:
  grapple::foundation::Result<grapple::render::SourceFrame> frameAt(
    const grapple::render::SourceFrameRequest& request
  ) override {
    ++requests;
    lastRequest = request;
    return grapple::render::SourceFrame{
      request.assetId,
      request.sourceTime,
      grapple::foundation::Resolution{2, 1},
      {10, 20, 30, 255, 40, 50, 60, 255}
    };
  }

  int requests = 0;
  std::optional<grapple::render::SourceFrameRequest> lastRequest;
};

class ThreePixelFrameSource final : public grapple::render::IRenderFrameSource {
public:
  grapple::foundation::Result<grapple::render::SourceFrame> frameAt(
    const grapple::render::SourceFrameRequest& request
  ) override {
    return grapple::render::SourceFrame{
      request.assetId,
      request.sourceTime,
      grapple::foundation::Resolution{3, 1},
      {
        10, 20, 30, 255,
        40, 50, 60, 255,
        70, 80, 90, 255
      }
    };
  }
};

class TwoRowFrameSource final : public grapple::render::IRenderFrameSource {
public:
  grapple::foundation::Result<grapple::render::SourceFrame> frameAt(
    const grapple::render::SourceFrameRequest& request
  ) override {
    return grapple::render::SourceFrame{
      request.assetId,
      request.sourceTime,
      grapple::foundation::Resolution{1, 2},
      {
        10, 20, 30, 255,
        40, 50, 60, 255
      }
    };
  }
};

class FourPixelFrameSource final : public grapple::render::IRenderFrameSource {
public:
  grapple::foundation::Result<grapple::render::SourceFrame> frameAt(
    const grapple::render::SourceFrameRequest& request
  ) override {
    return grapple::render::SourceFrame{
      request.assetId,
      request.sourceTime,
      grapple::foundation::Resolution{2, 2},
      {
        10, 20, 30, 255,
        40, 50, 60, 255,
        70, 80, 90, 255,
        100, 110, 120, 255
      }
    };
  }
};

class CapturingRangeSink final : public grapple::render::IRenderRangeSink {
public:
  grapple::foundation::Result<void> writeFrame(
    std::size_t frameIndex,
    const grapple::render::RenderFrameResult& frame
  ) override {
    frameIndexes.push_back(frameIndex);
    frameTimes.push_back(frame.frame.time);
    frameDescriptions.push_back(frame.frame.description);
    frameImages.push_back(frame.frame.image);
    frameAudioClips.push_back(frame.frame.audioClips);
    return {};
  }

  std::vector<std::size_t> frameIndexes;
  std::vector<grapple::foundation::TimeSeconds> frameTimes;
  std::vector<std::string> frameDescriptions;
  std::vector<std::optional<grapple::render::RenderedImage>> frameImages;
  std::vector<std::vector<grapple::render::RenderedAudioClip>> frameAudioClips;
};

class FailingRangeSink final : public grapple::render::IRenderRangeSink {
public:
  grapple::foundation::Result<void> writeFrame(
    std::size_t frameIndex,
    const grapple::render::RenderFrameResult& frame
  ) override {
    (void)frame;
    ++attempts;
    if (frameIndex == failAtIndex) {
      return grapple::foundation::Error{"render.sink_failed", "Range sink failed while writing a frame."};
    }
    return {};
  }

  std::size_t failAtIndex = 1;
  int attempts = 0;
};

class ImageShiftCameraRuntime final : public grapple::runtime::IEffectRuntime {
public:
  bool supports(const grapple::projection::RenderEffectNode& node) const override {
    return node.payload.implementation.kind == grapple::timeline::EffectImplementationKind::Python;
  }

  grapple::foundation::Result<grapple::runtime::EffectPrepareResult> prepare(
    const grapple::runtime::EffectPrepareRequest& request
  ) override {
    return grapple::runtime::EffectPrepareResult{
      grapple::runtime::PreparedEffectNode{
        request.graph.id,
        request.graph.targetNodeId,
        request.node.sourceNodeId,
        request.node.payload.activeRange,
        nullptr,
        grapple::runtime::RuntimeParamSet{},
        {}
      },
      {}
    };
  }

  grapple::foundation::Result<grapple::runtime::EffectProcessResult> process(
    const grapple::runtime::EffectProcessRequest& request
  ) override {
    return grapple::runtime::EffectProcessResult{
      grapple::runtime::RuntimeEffectOutput{
        request.prepared.effectGraphId,
        request.prepared.targetNodeId,
        request.prepared.sourceNodeId,
        {
          grapple::runtime::RuntimeNamedValue{
            grapple::runtime::output_name::CameraTransform,
            grapple::runtime::RuntimeValue{
              grapple::timeline::Transform2D{
                grapple::foundation::Vec2{0.5, 0.0},
                grapple::foundation::Vec2{1.0, 1.0},
                0.0,
                1.0
              }
            }
          }
        }
      },
      {}
    };
  }
};

class ImageZoomCameraRuntime final : public grapple::runtime::IEffectRuntime {
public:
  bool supports(const grapple::projection::RenderEffectNode& node) const override {
    return node.payload.implementation.kind == grapple::timeline::EffectImplementationKind::Python;
  }

  grapple::foundation::Result<grapple::runtime::EffectPrepareResult> prepare(
    const grapple::runtime::EffectPrepareRequest& request
  ) override {
    return grapple::runtime::EffectPrepareResult{
      grapple::runtime::PreparedEffectNode{
        request.graph.id,
        request.graph.targetNodeId,
        request.node.sourceNodeId,
        request.node.payload.activeRange,
        nullptr,
        grapple::runtime::RuntimeParamSet{},
        {}
      },
      {}
    };
  }

  grapple::foundation::Result<grapple::runtime::EffectProcessResult> process(
    const grapple::runtime::EffectProcessRequest& request
  ) override {
    return grapple::runtime::EffectProcessResult{
      grapple::runtime::RuntimeEffectOutput{
        request.prepared.effectGraphId,
        request.prepared.targetNodeId,
        request.prepared.sourceNodeId,
        {
          grapple::runtime::RuntimeNamedValue{
            grapple::runtime::output_name::CameraTransform,
            grapple::runtime::RuntimeValue{
              grapple::timeline::Transform2D{
                grapple::foundation::Vec2{0.0, 0.0},
                grapple::foundation::Vec2{2.0, 2.0},
                0.0,
                1.0
              }
            }
          }
        }
      },
      {}
    };
  }
};

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
    {},
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
          grapple::timeline::Transform2D{}
        }
      }
    },
    {
      grapple::projection::RenderAudioClip{
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
          grapple::timeline::Transform2D{}
        }
      }
    },
    {},
    {},
    {}
  };
}

grapple::projection::RenderPlan makeCameraEffectRenderPlan() {
  grapple::projection::RenderPlan plan = makeRenderPlan();
  plan.cameras.push_back(grapple::projection::RenderCamera{
    grapple::foundation::NodeId{"node_camera"},
    "Camera",
    grapple::timeline::Transform2D{},
    grapple::timeline::CameraLens{35.0}
  });
  const std::string source = "def prepare(ctx): return {'camera_transform': ctx.camera.transform}\n";
  plan.effectGraphs.push_back(grapple::projection::RenderEffectGraph{
    grapple::foundation::GraphId{"effect_graph_node_camera"},
    grapple::foundation::NodeId{"node_camera"},
    {
      grapple::projection::RenderEffectNode{
        grapple::foundation::NodeId{"node_camera_effect"},
        grapple::timeline::EffectPayload{
          "Camera Transform",
          grapple::timeline::EffectImplementation{
            grapple::timeline::EffectImplementationKind::Python,
            "prepare",
            grapple::timeline::EffectSource{
              grapple::timeline::EffectSourceKind::InlineSource,
              "python",
              source,
              std::nullopt,
              grapple::foundation::stableHash(source)
            }
          },
          grapple::timeline::EffectPortSet{
            {grapple::timeline::EffectPort{"camera"}}
          },
          grapple::timeline::ParamSet{},
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            grapple::foundation::TimeSeconds{12.0}
          }
        }
      }
    },
    {
      grapple::projection::RenderEffectEdge{
        grapple::foundation::EdgeId{"edge_camera_effect_targets_camera"},
        grapple::foundation::NodeId{"node_camera_effect"},
        grapple::graph::PortName{grapple::runtime::output_name::CameraTransform},
        grapple::foundation::NodeId{"node_camera"},
        grapple::graph::PortName{"input"},
        0
      }
    }
  });
  return plan;
}

grapple::projection::RenderPlan makeClipTransformRenderPlan(grapple::timeline::Transform2D transform) {
  grapple::projection::RenderPlan plan = makeRenderPlan();
  plan.clips[0].payload.transform = transform;
  return plan;
}

grapple::projection::RenderPlan makeClipTransformRenderPlan() {
  return makeClipTransformRenderPlan(grapple::timeline::Transform2D{
    grapple::foundation::Vec2{0.5, 0.0},
    grapple::foundation::Vec2{1.0, 1.0},
    0.0,
    1.0
  });
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
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.sourceRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.renderPlanHash == coreAfterLoad.preparedPlanHash.value());
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.time == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.description == "layers=1 clips=1 audioClips=1 cameras=0 effects=0");
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames.size() == 1);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips.size() == 1);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.cameras.empty());
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].clipNodeId == foundation::NodeId{"node_clip"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].trackNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].kind == render::RenderedMediaKind::Video);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].sourceTime == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.mediaFrames[0].transform == timeline::Transform2D{});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].clipNodeId == foundation::NodeId{"node_audio_clip"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].trackNodeId == foundation::NodeId{"node_track"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].assetId == foundation::AssetId{"asset_audio"});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].timelineRange.start == foundation::TimeSeconds{0.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].timelineRange.end == foundation::TimeSeconds{6.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].sourceRange.start == foundation::TimeSeconds{10.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].sourceRange.end == foundation::TimeSeconds{16.0});
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.audioClips[0].playbackRate == 1.0);
  GRAPPLE_REQUIRE(renderedActiveFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(renderedActiveFrame.value().renderDiagnostics.empty());

  const auto renderedInactiveFrame = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{8.0},
    render::RenderQuality::Final
  });
  GRAPPLE_REQUIRE(renderedInactiveFrame);
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.sourceRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.renderPlanHash == coreAfterLoad.preparedPlanHash.value());
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.description == "layers=1 clips=0 audioClips=0 cameras=0 effects=0");
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.mediaFrames.empty());
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.audioClips.empty());
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.cameras.empty());

  const auto pause = preview.pause();
  GRAPPLE_REQUIRE(pause);

  const auto stateAfterPause = preview.state();
  GRAPPLE_REQUIRE(stateAfterPause.playback == render::PreviewPlaybackState::Paused);

  const render::ExportSettings exportSettings = makeExportSettings(foundation::Resolution{3840, 2160});
  CapturingRangeSink finalSink;
  const auto finalResult = finalRender.render(render::FinalRenderRequest{exportSettings, &finalSink});
  GRAPPLE_REQUIRE(finalResult);
  GRAPPLE_REQUIRE(finalResult.value().outputPath.value == "/exports/test.mov");
  GRAPPLE_REQUIRE(finalResult.value().sourceRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(finalResult.value().renderPlanHash == coreAfterLoad.preparedPlanHash.value());
  GRAPPLE_REQUIRE(finalResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(finalResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(finalResult.value().renderDiagnostics.empty());
  GRAPPLE_REQUIRE((finalSink.frameIndexes == std::vector<std::size_t>{0, 1}));
  GRAPPLE_REQUIRE((finalSink.frameTimes == std::vector<foundation::TimeSeconds>{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{0.5}}));
  GRAPPLE_REQUIRE(finalSink.frameDescriptions.size() == 2);
  GRAPPLE_REQUIRE(finalSink.frameDescriptions[0] == "layers=1 clips=1 audioClips=1 cameras=0 effects=0");
  GRAPPLE_REQUIRE(finalSink.frameAudioClips.size() == 2);
  GRAPPLE_REQUIRE(finalSink.frameAudioClips[0].size() == 1);
  GRAPPLE_REQUIRE(finalSink.frameAudioClips[0][0].assetId == foundation::AssetId{"asset_audio"});

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

  FailingRangeSink failingSink;
  const auto failedFinalWrite = finalRender.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080}),
    &failingSink
  });
  GRAPPLE_REQUIRE(!failedFinalWrite);
  GRAPPLE_REQUIRE(failedFinalWrite.error().code == "render.sink_failed");
  GRAPPLE_REQUIRE(failingSink.attempts == 2);

  runtime::RuntimeEvaluator systemRuntime;
  render::LocalRenderCore systemCore{systemRuntime};
  render::LocalRenderSystem localRenderSystem{systemCore};

  const auto systemFrameBeforeLoad = localRenderSystem.renderPlaybackFrame(render::PlaybackFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(!systemFrameBeforeLoad);
  GRAPPLE_REQUIRE(systemFrameBeforeLoad.error().code == "render.plan_missing");

  const auto systemExportBeforeLoad = localRenderSystem.exportRange(render::ExportRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(!systemExportBeforeLoad);
  GRAPPLE_REQUIRE(systemExportBeforeLoad.error().code == "render.plan_missing");

  const auto systemLoad = localRenderSystem.loadPlan(plan);
  GRAPPLE_REQUIRE(systemLoad);
  const auto systemStateAfterLoad = localRenderSystem.state();
  GRAPPLE_REQUIRE(systemStateAfterLoad.core.hasPlan);
  GRAPPLE_REQUIRE(systemStateAfterLoad.core.preparedPlanHash == coreAfterLoad.preparedPlanHash);

  const auto systemSeek = localRenderSystem.seek(foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(systemSeek);
  const auto systemPlay = localRenderSystem.play();
  GRAPPLE_REQUIRE(systemPlay);
  const auto systemFrame = localRenderSystem.renderPlaybackFrame(render::PlaybackFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(systemFrame);
  GRAPPLE_REQUIRE(systemFrame.value().frame.description == "layers=1 clips=1 audioClips=1 cameras=0 effects=0");
  const auto systemPause = localRenderSystem.pause();
  GRAPPLE_REQUIRE(systemPause);

  CapturingRangeSink systemExportSink;
  const auto systemExport = localRenderSystem.exportRange(render::ExportRequest{
    makeExportSettings(foundation::Resolution{1920, 1080}),
    &systemExportSink
  });
  GRAPPLE_REQUIRE(systemExport);
  GRAPPLE_REQUIRE(systemExport.value().sourceRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(systemExport.value().renderPlanHash == systemStateAfterLoad.core.preparedPlanHash.value());
  GRAPPLE_REQUIRE(systemExport.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE((systemExportSink.frameTimes == std::vector<foundation::TimeSeconds>{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{0.5}}));
  const auto systemStateAfterExport = localRenderSystem.state();
  GRAPPLE_REQUIRE(systemStateAfterExport.playback == render::PreviewPlaybackState::Paused);
  GRAPPLE_REQUIRE(systemStateAfterExport.playhead == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(systemStateAfterExport.lastExportSettings.has_value());
  GRAPPLE_REQUIRE((systemStateAfterExport.lastExportSettings->resolution == foundation::Resolution{1920, 1080}));
  GRAPPLE_REQUIRE(systemStateAfterExport.core.preparedPlanHash == systemStateAfterLoad.core.preparedPlanHash);

  grapple::projection::RenderPlan emptyLoadedPlan = plan;
  emptyLoadedPlan.layers.clear();
  emptyLoadedPlan.audioTracks.clear();
  emptyLoadedPlan.clips.clear();
  emptyLoadedPlan.audioClips.clear();
  const auto systemLoadEmpty = localRenderSystem.loadPlan(emptyLoadedPlan);
  GRAPPLE_REQUIRE(systemLoadEmpty);
  CapturingRangeSink exactPlanExportSink;
  const auto exactPlanExport = localRenderSystem.exportPlanRange(render::ExportPlanRequest{
    plan,
    makeExportSettings(foundation::Resolution{1920, 1080}),
    &exactPlanExportSink
  });
  GRAPPLE_REQUIRE(exactPlanExport);
  GRAPPLE_REQUIRE(exactPlanExport.value().sourceRevision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(exactPlanExport.value().renderPlanHash == systemStateAfterLoad.core.preparedPlanHash.value());
  GRAPPLE_REQUIRE(exactPlanExport.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(!exactPlanExportSink.frameDescriptions.empty());
  GRAPPLE_REQUIRE(exactPlanExportSink.frameDescriptions[0] == "layers=1 clips=1 audioClips=1 cameras=0 effects=0");

  TestFrameSource frameSource;
  render::LocalRenderCore imageCore{runtime, frameSource};
  render::PreviewRenderShell imagePreview{imageCore};
  const auto imageLoad = imageCore.loadPlan(plan);
  GRAPPLE_REQUIRE(imageLoad);
  const auto imageFrame = imagePreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(imageFrame);
  GRAPPLE_REQUIRE(imageFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE((imageFrame.value().frame.image->resolution == foundation::Resolution{2, 1}));
  GRAPPLE_REQUIRE((imageFrame.value().frame.image->rgbaPixels == std::vector<std::uint8_t>{10, 20, 30, 255, 40, 50, 60, 255}));
  GRAPPLE_REQUIRE(frameSource.requests == 1);
  GRAPPLE_REQUIRE(frameSource.lastRequest.has_value());
  GRAPPLE_REQUIRE(frameSource.lastRequest->assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(frameSource.lastRequest->sourceTime == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(frameSource.lastRequest->quality == render::RenderQuality::Draft);

  TestFrameSource shiftedClipFrameSource;
  render::LocalRenderCore shiftedClipImageCore{runtime, shiftedClipFrameSource};
  render::PreviewRenderShell shiftedClipImagePreview{shiftedClipImageCore};
  const auto shiftedClipImageLoad = shiftedClipImageCore.loadPlan(makeClipTransformRenderPlan());
  GRAPPLE_REQUIRE(shiftedClipImageLoad);
  const auto shiftedClipImageFrame = shiftedClipImagePreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(shiftedClipImageFrame);
  GRAPPLE_REQUIRE(shiftedClipImageFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE(shiftedClipImageFrame.value().frame.mediaFrames.size() == 1);
  GRAPPLE_REQUIRE(shiftedClipImageFrame.value().frame.mediaFrames[0].transform.position.x == 0.5);
  GRAPPLE_REQUIRE((shiftedClipImageFrame.value().frame.image->rgbaPixels == std::vector<std::uint8_t>{0, 0, 0, 0, 10, 20, 30, 255}));

  TestFrameSource shiftedClipFinalFrameSource;
  render::LocalRenderCore shiftedClipFinalImageCore{runtime, shiftedClipFinalFrameSource};
  render::FinalRenderShell shiftedClipFinalRender{shiftedClipFinalImageCore};
  const auto shiftedClipFinalImageLoad = shiftedClipFinalImageCore.loadPlan(makeClipTransformRenderPlan());
  GRAPPLE_REQUIRE(shiftedClipFinalImageLoad);
  CapturingRangeSink shiftedClipFinalSink;
  const auto shiftedClipFinalResult = shiftedClipFinalRender.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080}),
    &shiftedClipFinalSink
  });
  GRAPPLE_REQUIRE(shiftedClipFinalResult);
  GRAPPLE_REQUIRE(shiftedClipFinalSink.frameImages.size() == 2);
  GRAPPLE_REQUIRE(shiftedClipFinalSink.frameImages[0].has_value());
  GRAPPLE_REQUIRE((shiftedClipFinalSink.frameImages[0]->rgbaPixels == shiftedClipImageFrame.value().frame.image->rgbaPixels));

  TwoRowFrameSource yShiftClipFrameSource;
  render::LocalRenderCore yShiftClipImageCore{runtime, yShiftClipFrameSource};
  render::PreviewRenderShell yShiftClipImagePreview{yShiftClipImageCore};
  const auto yShiftClipImageLoad = yShiftClipImageCore.loadPlan(makeClipTransformRenderPlan(grapple::timeline::Transform2D{
    grapple::foundation::Vec2{0.0, 0.5},
    grapple::foundation::Vec2{1.0, 1.0},
    0.0,
    1.0
  }));
  GRAPPLE_REQUIRE(yShiftClipImageLoad);
  const auto yShiftClipImageFrame = yShiftClipImagePreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(yShiftClipImageFrame);
  GRAPPLE_REQUIRE(yShiftClipImageFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE((yShiftClipImageFrame.value().frame.image->rgbaPixels == std::vector<std::uint8_t>{40, 50, 60, 255, 0, 0, 0, 0}));

  TestFrameSource opacityClipFrameSource;
  render::LocalRenderCore opacityClipImageCore{runtime, opacityClipFrameSource};
  render::PreviewRenderShell opacityClipImagePreview{opacityClipImageCore};
  const auto opacityClipImageLoad = opacityClipImageCore.loadPlan(makeClipTransformRenderPlan(grapple::timeline::Transform2D{
    grapple::foundation::Vec2{0.0, 0.0},
    grapple::foundation::Vec2{1.0, 1.0},
    0.0,
    0.5
  }));
  GRAPPLE_REQUIRE(opacityClipImageLoad);
  const auto opacityClipImageFrame = opacityClipImagePreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(opacityClipImageFrame);
  GRAPPLE_REQUIRE(opacityClipImageFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE((opacityClipImageFrame.value().frame.image->rgbaPixels == std::vector<std::uint8_t>{10, 20, 30, 128, 40, 50, 60, 128}));

  FourPixelFrameSource rotatedClipFrameSource;
  render::LocalRenderCore rotatedClipImageCore{runtime, rotatedClipFrameSource};
  render::PreviewRenderShell rotatedClipImagePreview{rotatedClipImageCore};
  const auto rotatedClipImageLoad = rotatedClipImageCore.loadPlan(makeClipTransformRenderPlan(grapple::timeline::Transform2D{
    grapple::foundation::Vec2{0.0, 0.0},
    grapple::foundation::Vec2{1.0, 1.0},
    180.0,
    1.0
  }));
  GRAPPLE_REQUIRE(rotatedClipImageLoad);
  const auto rotatedClipImageFrame = rotatedClipImagePreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{4.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(rotatedClipImageFrame);
  GRAPPLE_REQUIRE(rotatedClipImageFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE((rotatedClipImageFrame.value().frame.image->rgbaPixels == std::vector<std::uint8_t>{
    100, 110, 120, 255,
    70, 80, 90, 255,
    40, 50, 60, 255,
    10, 20, 30, 255
  }));

  ImageShiftCameraRuntime imageShiftRuntime;
  runtime::RuntimeEvaluator imageShiftEvaluator{{&imageShiftRuntime}};
  TestFrameSource shiftedFrameSource;
  render::LocalRenderCore shiftedImageCore{imageShiftEvaluator, shiftedFrameSource};
  render::PreviewRenderShell shiftedImagePreview{shiftedImageCore};
  const auto shiftedImageLoad = shiftedImageCore.loadPlan(makeCameraEffectRenderPlan());
  GRAPPLE_REQUIRE(shiftedImageLoad);
  const auto shiftedImageFrame = shiftedImagePreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(shiftedImageFrame);
  GRAPPLE_REQUIRE(shiftedImageFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE((shiftedImageFrame.value().frame.image->resolution == foundation::Resolution{2, 1}));
  GRAPPLE_REQUIRE((shiftedImageFrame.value().frame.image->rgbaPixels == std::vector<std::uint8_t>{40, 50, 60, 255, 0, 0, 0, 0}));
  GRAPPLE_REQUIRE(shiftedImageFrame.value().runtimeDiagnostics.empty());

  ImageZoomCameraRuntime imageZoomRuntime;
  runtime::RuntimeEvaluator imageZoomEvaluator{{&imageZoomRuntime}};
  ThreePixelFrameSource zoomFrameSource;
  render::LocalRenderCore zoomImageCore{imageZoomEvaluator, zoomFrameSource};
  render::PreviewRenderShell zoomImagePreview{zoomImageCore};
  const auto zoomImageLoad = zoomImageCore.loadPlan(makeCameraEffectRenderPlan());
  GRAPPLE_REQUIRE(zoomImageLoad);
  const auto zoomImageFrame = zoomImagePreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{0.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(zoomImageFrame);
  GRAPPLE_REQUIRE(zoomImageFrame.value().frame.image.has_value());
  GRAPPLE_REQUIRE((zoomImageFrame.value().frame.image->resolution == foundation::Resolution{3, 1}));
  GRAPPLE_REQUIRE((zoomImageFrame.value().frame.image->rgbaPixels == std::vector<std::uint8_t>{
    40, 50, 60, 255,
    40, 50, 60, 255,
    70, 80, 90, 255
  }));
  GRAPPLE_REQUIRE(zoomImageFrame.value().runtimeDiagnostics.empty());

  TestFrameSource finalRangeFrameSource;
  render::LocalRenderCore finalRangeImageCore{imageShiftEvaluator, finalRangeFrameSource};
  render::FinalRenderShell finalRangeRender{finalRangeImageCore};
  const auto finalRangeImageLoad = finalRangeImageCore.loadPlan(makeCameraEffectRenderPlan());
  GRAPPLE_REQUIRE(finalRangeImageLoad);
  CapturingRangeSink finalRangeImageSink;
  const auto finalRangeImageResult = finalRangeRender.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080}),
    &finalRangeImageSink
  });
  GRAPPLE_REQUIRE(finalRangeImageResult);
  GRAPPLE_REQUIRE(finalRangeImageResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(finalRangeImageResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(finalRangeImageSink.frameImages.size() == 2);
  GRAPPLE_REQUIRE(finalRangeImageSink.frameImages[0].has_value());
  GRAPPLE_REQUIRE((finalRangeImageSink.frameImages[0]->rgbaPixels == std::vector<std::uint8_t>{40, 50, 60, 255, 0, 0, 0, 0}));
  GRAPPLE_REQUIRE(finalRangeFrameSource.requests == 2);
  GRAPPLE_REQUIRE(finalRangeFrameSource.lastRequest.has_value());
  GRAPPLE_REQUIRE(finalRangeFrameSource.lastRequest->assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(finalRangeFrameSource.lastRequest->sourceTime == foundation::TimeSeconds{0.5});
  GRAPPLE_REQUIRE(finalRangeFrameSource.lastRequest->quality == render::RenderQuality::Final);

  CameraTransformRuntime cameraRuntime;
  runtime::RuntimeEvaluator cameraEvaluator{{&cameraRuntime}};
  render::LocalRenderCore cameraCore{cameraEvaluator};
  render::PreviewRenderShell cameraPreview{cameraCore};
  render::FinalRenderShell cameraFinal{cameraCore};
  const auto cameraLoad = cameraCore.loadPlan(makeCameraEffectRenderPlan());
  GRAPPLE_REQUIRE(cameraLoad);
  GRAPPLE_REQUIRE(cameraRuntime.prepareCount == 1);
  const auto sameCameraLoad = cameraCore.loadPlan(makeCameraEffectRenderPlan());
  GRAPPLE_REQUIRE(sameCameraLoad);
  GRAPPLE_REQUIRE(cameraRuntime.prepareCount == 1);
  projection::RenderPlan revisedCameraPlan = makeCameraEffectRenderPlan();
  revisedCameraPlan.revision = foundation::RevisionId{"rev_4"};
  const auto revisedCameraLoad = cameraCore.loadPlan(revisedCameraPlan);
  GRAPPLE_REQUIRE(revisedCameraLoad);
  GRAPPLE_REQUIRE(cameraRuntime.prepareCount == 1);
  GRAPPLE_REQUIRE(cameraCore.state().revision == foundation::RevisionId{"rev_4"});
  const auto cameraFrame = cameraPreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{2.5},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(cameraFrame);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.sourceRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(cameraFrame.value().frame.renderPlanHash == cameraCore.state().preparedPlanHash.value());
  GRAPPLE_REQUIRE(cameraFrame.value().frame.description == "layers=1 clips=1 audioClips=1 cameras=1 effects=1");
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras.size() == 1);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].cameraNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].state.transform.position.x == 2.5);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].state.transform.position.y == 5.0);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].state.transform.rotationDegrees == 25.0);
  GRAPPLE_REQUIRE(cameraFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(cameraRuntime.processCount == 1);
  const auto cameraFinalResult = cameraFinal.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(cameraFinalResult);
  GRAPPLE_REQUIRE(cameraFinalResult.value().sourceRevision == foundation::RevisionId{"rev_4"});
  GRAPPLE_REQUIRE(cameraFinalResult.value().renderPlanHash == cameraCore.state().preparedPlanHash.value());
  GRAPPLE_REQUIRE(cameraFinalResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(cameraFinalResult.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(cameraRuntime.processCount == 3);

  CameraTransformRuntime invalidCameraRuntime{true};
  runtime::RuntimeEvaluator invalidCameraEvaluator{{&invalidCameraRuntime}};
  render::LocalRenderCore invalidCameraCore{invalidCameraEvaluator};
  render::FinalRenderShell invalidCameraFinal{invalidCameraCore};
  const auto invalidCameraLoad = invalidCameraCore.loadPlan(makeCameraEffectRenderPlan());
  GRAPPLE_REQUIRE(invalidCameraLoad);
  const auto invalidCameraFinalResult = invalidCameraFinal.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(invalidCameraFinalResult);
  GRAPPLE_REQUIRE(invalidCameraFinalResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(invalidCameraFinalResult.value().runtimeDiagnostics.size() == 2);
  GRAPPLE_REQUIRE(invalidCameraFinalResult.value().runtimeDiagnostics[0].code == "runtime.camera_transform_output_invalid");
  GRAPPLE_REQUIRE(invalidCameraFinalResult.value().runtimeDiagnostics[0].location.nodeId == foundation::NodeId{"node_camera_effect"});

  return 0;
}
