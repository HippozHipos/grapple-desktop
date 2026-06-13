#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectSerializer.hpp>
#include <grapple/projection/ProjectionQueryService.hpp>
#include <grapple/render/FinalRenderShell.hpp>
#include <grapple/render/PreviewRenderShell.hpp>
#include <grapple/runtime/EffectRuntime.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>
#include <grapple/storage/ProjectPackageManifest.hpp>
#include <grapple/storage/ProjectPackageStore.hpp>
#include <grapple/storage/ProjectPackageWriter.hpp>

#include <TestAssert.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

class TestEffectRuntime final : public grapple::runtime::IEffectRuntime {
public:
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
        grapple::runtime::RuntimeParamSet{},
        {
          grapple::runtime::RuntimeNamedValue{
            "source_hash",
            grapple::runtime::RuntimeValue{request.node.payload.implementation.source.sourceHash.toHex()}
          }
        }
      },
      {}
    };
  }

  grapple::foundation::Result<grapple::runtime::EffectProcessResult> process(
    const grapple::runtime::EffectProcessRequest& request
  ) override {
    ++processCount;
    return grapple::runtime::EffectProcessResult{
      grapple::runtime::RuntimeEffectOutput{
        request.prepared.effectGraphId,
        request.prepared.targetNodeId,
        request.prepared.sourceNodeId,
        {
          grapple::runtime::RuntimeNamedValue{
            "time",
            grapple::runtime::RuntimeValue{request.time.value}
          }
        }
      },
      {}
    };
  }

  int prepareCount = 0;
  int processCount = 0;
};

grapple::foundation::Result<grapple::project::ProjectCommandResult> applyAndCommit(
  grapple::project::ProjectController& controller,
  grapple::storage::ProjectPackageStore& store,
  const grapple::project::ProjectCommandEnvelope& command
) {
  auto result = controller.apply(command);
  if (!result) {
    return result.error();
  }

  auto snapshot = controller.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto commit = store.commit(grapple::storage::makeAtomicProjectCommit(
    snapshot.value(),
    command,
    result.value(),
    grapple::storage::ProjectCommitRecordOptions{
      std::chrono::system_clock::now(),
      std::nullopt
    }
  ));
  if (!commit) {
    return commit.error();
  }

  return result.value();
}

} // namespace

int main() {
  using namespace grapple;

  project::ProjectController controller{
    project::createEmptyProject(foundation::ProjectId{"proj_native_flow"}, "Native Flow")
  };
  storage::ProjectPackageStore store{storage::ProjectPackage{
    foundation::ProjectId{"proj_native_flow"},
    foundation::FilePath{"native-flow.grapple"},
    1
  }};

  const auto initial = controller.snapshot();
  GRAPPLE_REQUIRE(initial);

  const auto registerAsset = applyAndCommit(controller, store, project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_asset"},
    foundation::ProjectId{"proj_native_flow"},
    initial.value().revision,
    project::CommandSource{project::CommandSourceKind::Importer, std::nullopt, "integration"},
    project::RegisterAssetCommand{
      asset::Asset{
        foundation::AssetId{"asset_video"},
        "Video",
        asset::AssetMetadata{
          asset::AssetMediaType::Video,
          foundation::FilePath{"/media/video.mp4"},
          std::nullopt,
          foundation::TimeSeconds{2.0},
          foundation::Resolution{1920, 1080},
          foundation::FrameRate{24, 1}
        }
      }
    }
  });
  GRAPPLE_REQUIRE(registerAsset);

  const auto composition = applyAndCommit(controller, store, project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_composition"},
    foundation::ProjectId{"proj_native_flow"},
    registerAsset.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "integration"},
    project::CreateCompositionCommand{foundation::NodeId{"node_composition"}, "Main"}
  });
  GRAPPLE_REQUIRE(composition);

  const auto track = applyAndCommit(controller, store, project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_track"},
    foundation::ProjectId{"proj_native_flow"},
    composition.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "integration"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_track"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_track"},
      "Video"
    }
  });
  GRAPPLE_REQUIRE(track);

  const auto clip = applyAndCommit(controller, store, project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_clip"},
    foundation::ProjectId{"proj_native_flow"},
    track.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "integration"},
    project::CreateClipCommand{
      foundation::NodeId{"node_clip"},
      foundation::NodeId{"node_track"},
      foundation::EdgeId{"edge_contains_clip"},
      timeline::ClipPayload{
        timeline::ClipKind::Video,
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{2.0}},
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{2.0}},
        1.0,
        foundation::AssetId{"asset_video"},
        timeline::Transform{}
      }
    }
  });
  GRAPPLE_REQUIRE(clip);

  const auto camera = applyAndCommit(controller, store, project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_camera"},
    foundation::ProjectId{"proj_native_flow"},
    clip.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "integration"},
    project::CreateCameraCommand{
      foundation::NodeId{"node_camera"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_camera"},
      timeline::CameraPayload{
        "Camera",
        timeline::Transform{},
        timeline::CameraLens{35.0}
      }
    }
  });
  GRAPPLE_REQUIRE(camera);

  const std::string effectSource = "def prepare(ctx):\n  return {'ok': True}\n";
  const auto effect = applyAndCommit(controller, store, project::ProjectCommandEnvelope{
    foundation::CommandId{"cmd_effect"},
    foundation::ProjectId{"proj_native_flow"},
    camera.value().afterRevision,
    project::CommandSource{project::CommandSourceKind::Agent, foundation::RunId{"run_native_flow"}, "agent"},
    project::CreateEffectCommand{
      foundation::NodeId{"node_effect"},
      foundation::NodeId{"node_camera"},
      foundation::EdgeId{"edge_effect_targets_camera"},
      timeline::EffectPayload{
        "Camera Effect",
        timeline::EffectImplementation{
          timeline::EffectImplementationKind::Python,
          "prepare",
          timeline::EffectSource{
            timeline::EffectSourceKind::InlineSource,
            "python",
            effectSource,
            std::nullopt,
            foundation::stableHash(effectSource)
          }
        },
        timeline::EffectPortSet{
          {timeline::EffectPort{"frame"}},
          {timeline::EffectPort{"camera"}}
        },
        timeline::ParamSet{
          {timeline::Param{
            "strength",
            1.0,
            timeline::Param::Control{
              "Strength",
              timeline::Param::NumericControl{0.0, 1.0, 0.01}
            }
          }}
        },
        foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{2.0}}
      },
      graph::PortName{"camera"},
      graph::PortName{"input"},
      0
    }
  });
  GRAPPLE_REQUIRE(effect);

  GRAPPLE_REQUIRE(store.state().head.has_value());
  GRAPPLE_REQUIRE(store.state().head->currentRevision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(store.state().commandLog.records().size() == 6);
  GRAPPLE_REQUIRE(store.state().eventLog.records().size() == 12);

  const projection::ProjectionQueryService projectionQueries{controller};
  const auto renderPlan = projectionQueries.buildCurrentRenderPlan();
  GRAPPLE_REQUIRE(renderPlan);
  GRAPPLE_REQUIRE(renderPlan.value().plan.revision == foundation::RevisionId{"rev_6"});
  GRAPPLE_REQUIRE(renderPlan.value().plan.clips.size() == 1);
  GRAPPLE_REQUIRE(renderPlan.value().plan.cameras.size() == 1);
  GRAPPLE_REQUIRE(renderPlan.value().plan.effectGraphs.size() == 1);

  TestEffectRuntime effectRuntime;
  runtime::RuntimeEvaluator runtime{{&effectRuntime}};
  render::LocalRenderCore renderCore{runtime};
  render::PreviewRenderShell preview{renderCore};
  render::FinalRenderShell finalRender{renderCore};

  const auto load = renderCore.loadPlan(renderPlan.value().plan);
  GRAPPLE_REQUIRE(load);
  GRAPPLE_REQUIRE(effectRuntime.prepareCount == 1);

  const auto previewFrame = preview.renderFrame(render::RenderFrameRequest{
    foundation::TimeSeconds{1.0},
    render::RenderQuality::Draft
  });
  GRAPPLE_REQUIRE(previewFrame);
  GRAPPLE_REQUIRE(previewFrame.value().frame.description == "layers=1 clips=1 cameras=1 effects=1");
  GRAPPLE_REQUIRE(previewFrame.value().frame.mediaFrames.size() == 1);
  GRAPPLE_REQUIRE(previewFrame.value().frame.mediaFrames[0].assetId == foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(previewFrame.value().frame.mediaFrames[0].kind == render::RenderedMediaKind::Video);
  GRAPPLE_REQUIRE(previewFrame.value().frame.mediaFrames[0].sourceTime == foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(previewFrame.value().runtimeDiagnostics.empty());
  GRAPPLE_REQUIRE(effectRuntime.processCount == 1);

  const auto finalResult = finalRender.render(render::FinalRenderRequest{
    render::ExportSettings{
      foundation::TimeRange{foundation::TimeSeconds{0.0}, foundation::TimeSeconds{1.0}},
      foundation::FrameRate{2, 1},
      foundation::Resolution{1920, 1080},
      render::Codec{"test"},
      render::RenderQuality::Final,
      foundation::FilePath{"/tmp/native-flow.mov"}
    }
  });
  GRAPPLE_REQUIRE(finalResult);
  GRAPPLE_REQUIRE(finalResult.value().framesEvaluated == 2);
  GRAPPLE_REQUIRE(effectRuntime.processCount == 3);
  GRAPPLE_REQUIRE(finalRender.state().core.preparedPlanHash == renderCore.state().preparedPlanHash);

  const auto finalSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(finalSnapshot);
  const std::filesystem::path packageRoot =
    std::filesystem::temp_directory_path() /
    ("grapple_native_flow_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const storage::ProjectPackageWriter packageWriter;
  const auto packageManifest = storage::buildProjectPackageManifest(store.state());
  GRAPPLE_REQUIRE(packageManifest);
  const auto writtenManifestPath = packageWriter.writeManifest(
    packageManifest.value(),
    storage::ProjectPackage{
      foundation::ProjectId{"proj_native_flow"},
      foundation::FilePath{packageRoot.string()},
      1
    }
  );
  GRAPPLE_REQUIRE(writtenManifestPath);
  std::ifstream manifestFile{writtenManifestPath.value().value, std::ios::binary};
  GRAPPLE_REQUIRE(manifestFile.good());
  std::ostringstream manifestContents;
  manifestContents << manifestFile.rdbuf();
  GRAPPLE_REQUIRE(manifestContents.str() == storage::serializeCanonicalProjectPackageManifest(packageManifest.value()));

  const auto writtenSnapshotPath = packageWriter.writeSnapshot(storage::ProjectSnapshotWriteRequest{
    storage::ProjectPackage{
      foundation::ProjectId{"proj_native_flow"},
      foundation::FilePath{packageRoot.string()},
      1
    },
    finalSnapshot.value(),
    storage::SnapshotCommitRecord{
      foundation::SnapshotId{"snap_rev_6"},
      foundation::FilePath{"snapshots/rev_6.json"},
      std::optional<std::string>{"integration"}
    }
  });
  GRAPPLE_REQUIRE(writtenSnapshotPath);
  std::ifstream snapshotFile{writtenSnapshotPath.value().value, std::ios::binary};
  GRAPPLE_REQUIRE(snapshotFile.good());
  std::ostringstream snapshotContents;
  snapshotContents << snapshotFile.rdbuf();
  GRAPPLE_REQUIRE(snapshotContents.str() == project::serializeCanonicalProjectSnapshot(finalSnapshot.value()));
  std::filesystem::remove_all(packageRoot);

  return 0;
}
