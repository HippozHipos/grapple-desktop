#include <grapple/render/FinalRenderShell.hpp>
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
        nullptr,
        {}
      },
      {}
    };
  }

  grapple::foundation::Result<grapple::runtime::EffectProcessResult> process(
    const grapple::runtime::EffectProcessRequest& request
  ) override {
    ++processCount;
    grapple::runtime::RuntimeValue transformValue = grapple::timeline::Transform{
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
        nullptr,
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
              grapple::timeline::Transform{
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

grapple::projection::RenderPlan makeCameraEffectRenderPlan() {
  grapple::projection::RenderPlan plan = makeRenderPlan();
  plan.cameras.push_back(grapple::projection::RenderCamera{
    grapple::foundation::NodeId{"node_camera"},
    "Camera",
    grapple::timeline::Transform{},
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
  GRAPPLE_REQUIRE(renderedActiveFrame.value().frame.cameras.empty());
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
  GRAPPLE_REQUIRE(renderedInactiveFrame.value().frame.cameras.empty());

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

  TestFrameSource finalRangeFrameSource;
  render::LocalRenderCore finalRangeImageCore{imageShiftEvaluator, finalRangeFrameSource};
  render::FinalRenderShell finalRangeRender{finalRangeImageCore};
  const auto finalRangeImageLoad = finalRangeImageCore.loadPlan(makeCameraEffectRenderPlan());
  GRAPPLE_REQUIRE(finalRangeImageLoad);
  const auto finalRangeImageResult = finalRangeRender.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(finalRangeImageResult);
  GRAPPLE_REQUIRE(finalRangeImageResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(finalRangeImageResult.value().runtimeDiagnostics.empty());
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
  const auto cameraFrame = cameraPreview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{2.5},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(cameraFrame);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.description == "layers=1 clips=2 cameras=1 effects=1");
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras.size() == 1);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].cameraNodeId == foundation::NodeId{"node_camera"});
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].transform.position.x == 2.5);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].transform.position.y == 5.0);
  GRAPPLE_REQUIRE(cameraFrame.value().frame.cameras[0].transform.rotationDegrees == 25.0);
  GRAPPLE_REQUIRE(cameraFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(cameraRuntime.processCount == 1);
  const auto cameraFinalResult = cameraFinal.render(render::FinalRenderRequest{
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(cameraFinalResult);
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
