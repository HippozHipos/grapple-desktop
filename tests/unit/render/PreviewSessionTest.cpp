#include <grapple/render/PreviewSession.hpp>

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
        "Video",
        true
      }
    },
    {},
    {},
    {}
  };
}

} // namespace

int main() {
  using namespace grapple;

  runtime::RuntimeEvaluator runtime;
  render::PreviewSession session{runtime};

  const auto seekBeforeLoad = session.seek(foundation::TimeSeconds{1.0});
  GRAPPLE_REQUIRE(!seekBeforeLoad);
  GRAPPLE_REQUIRE(seekBeforeLoad.error().code == "render.preview_plan_missing");

  const auto load = session.loadPlan(makeRenderPlan());
  GRAPPLE_REQUIRE(load);

  const auto stateAfterLoad = session.state();
  GRAPPLE_REQUIRE(stateAfterLoad);
  GRAPPLE_REQUIRE(stateAfterLoad.value().hasPlan);
  GRAPPLE_REQUIRE(stateAfterLoad.value().revision == foundation::RevisionId{"rev_3"});
  GRAPPLE_REQUIRE(stateAfterLoad.value().preparedPlanHash.has_value());
  GRAPPLE_REQUIRE(stateAfterLoad.value().playback == render::PlaybackState::Paused);

  const auto seek = session.seek(foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(seek);

  const auto play = session.play();
  GRAPPLE_REQUIRE(play);

  const auto stateAfterPlay = session.state();
  GRAPPLE_REQUIRE(stateAfterPlay);
  GRAPPLE_REQUIRE(stateAfterPlay.value().playhead == foundation::TimeSeconds{4.0});
  GRAPPLE_REQUIRE(stateAfterPlay.value().playback == render::PlaybackState::Playing);

  const auto pause = session.pause();
  GRAPPLE_REQUIRE(pause);

  const auto stateAfterPause = session.state();
  GRAPPLE_REQUIRE(stateAfterPause);
  GRAPPLE_REQUIRE(stateAfterPause.value().playback == render::PlaybackState::Paused);

  return 0;
}
