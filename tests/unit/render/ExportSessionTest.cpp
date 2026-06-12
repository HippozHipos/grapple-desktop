#include <grapple/render/ExportSession.hpp>

#include <TestAssert.hpp>

namespace {

grapple::projection::RenderPlan makeRenderPlan() {
  return grapple::projection::RenderPlan{
    grapple::foundation::ProjectId{"proj_export"},
    grapple::foundation::RevisionId{"rev_7"},
    grapple::projection::RenderStage{"Export Test"},
    grapple::foundation::TimeSeconds{20.0},
    {},
    {
      grapple::projection::RenderLayer{
        grapple::foundation::NodeId{"node_track"},
        "Video",
        true
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
      grapple::foundation::TimeSeconds{10.0}
    },
    grapple::foundation::FrameRate{24, 1},
    resolution,
    grapple::render::Codec{"prores"},
    grapple::render::ExportQuality::Final,
    grapple::foundation::FilePath{"/exports/test.mov"}
  };
}

} // namespace

int main() {
  using namespace grapple;

  runtime::RuntimeEvaluator runtime;
  render::ExportSession session{runtime};

  const projection::RenderPlan plan = makeRenderPlan();
  const render::ExportSettings settings = makeExportSettings(foundation::Resolution{3840, 2160});
  const auto start = session.start(render::ExportRequest{plan, settings});
  GRAPPLE_REQUIRE(start);

  const auto state = session.state();
  GRAPPLE_REQUIRE(state);
  GRAPPLE_REQUIRE(state.value().hasExport);
  GRAPPLE_REQUIRE(state.value().revision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(state.value().preparedPlanHash.has_value());
  GRAPPLE_REQUIRE(state.value().settings.has_value());
  GRAPPLE_REQUIRE((state.value().settings->resolution == foundation::Resolution{3840, 2160}));
  GRAPPLE_REQUIRE(state.value().settings->outputPath.value == "/exports/test.mov");

  projection::RenderPlan samePlanDifferentJobSettings = makeRenderPlan();
  const auto changedSettingsStart = session.start(render::ExportRequest{
    samePlanDifferentJobSettings,
    makeExportSettings(foundation::Resolution{1920, 1080})
  });
  GRAPPLE_REQUIRE(changedSettingsStart);

  const auto stateAfterSettingsChange = session.state();
  GRAPPLE_REQUIRE(stateAfterSettingsChange);
  GRAPPLE_REQUIRE(stateAfterSettingsChange.value().revision == foundation::RevisionId{"rev_7"});
  GRAPPLE_REQUIRE(stateAfterSettingsChange.value().preparedPlanHash == state.value().preparedPlanHash);
  GRAPPLE_REQUIRE((stateAfterSettingsChange.value().settings->resolution == foundation::Resolution{1920, 1080}));

  return 0;
}
