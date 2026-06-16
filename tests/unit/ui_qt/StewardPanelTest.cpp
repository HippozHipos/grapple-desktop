#include <grapple/ui_qt/StewardPanel.hpp>

#include <grapple/agent/AgentConversationState.hpp>
#include <grapple/app/AppViewModel.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/timeline/Payloads.hpp>

#include "TestAssert.hpp"

#include <QApplication>
#include <QLabel>

#include <string>

namespace {

grapple::app::AppViewModel viewModelWithCamera() {
  grapple::app::AppViewModel viewModel;
  viewModel.project.projectId = grapple::foundation::ProjectId{"project"};
  viewModel.project.name = "Panel Route Test";
  viewModel.project.revision = grapple::foundation::RevisionId{"rev_1"};
  viewModel.timeline.duration = grapple::foundation::TimeSeconds{10.0};
  viewModel.timeline.cameras.push_back(grapple::app::AppCameraRow{
    grapple::foundation::NodeId{"camera_1"},
    "Camera",
    grapple::timeline::CameraState{
      grapple::timeline::Transform2D{},
      grapple::timeline::CameraLens{35.0}
    }
  });
  return viewModel;
}

bool containsText(const std::string& value, const std::string& text) {
  return value.find(text) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
  QApplication app{argc, argv};

  grapple::ui::StewardPanel panel;
  bool cameraRouteCalled = false;
  bool textRouteCalled = false;
  bool noteRouteCalled = false;
  bool startSampleCalled = false;

  panel.setStartSampleHandler([&] {
    startSampleCalled = true;
  });
  panel.setTextClipIntentTargetsTextHandler([](std::string intent) {
    return containsText(intent, "title") || containsText(intent, "caption") || containsText(intent, "text");
  });
  panel.setNoteIntentTargetsNoteHandler([](std::string intent) {
    return containsText(intent, "note") || containsText(intent, "rationale");
  });
  panel.setCreateCameraEffectHandler([&](std::string) {
    cameraRouteCalled = true;
  });
  panel.setTryCreateTextClipHandler([&](std::string) {
    textRouteCalled = true;
    return true;
  });
  panel.setTryCreateNoteHandler([&](std::string) {
    noteRouteCalled = true;
    return true;
  });

  panel.setViewModel(
    grapple::app::AppViewModel{},
    grapple::agent::AgentConversationState{},
    std::nullopt,
    std::nullopt
  );
  auto* editSummary = panel.findChild<QLabel*>("stewardEditSummary");
  GRAPPLE_REQUIRE(editSummary != nullptr);
  GRAPPLE_REQUIRE(editSummary->isHidden());
  GRAPPLE_REQUIRE(panel.primaryActionText() == "Start Sample");
  GRAPPLE_REQUIRE(panel.primaryActionEnabled());
  GRAPPLE_REQUIRE(panel.suggestedRequestCount() == 0);
  panel.triggerPrimaryAction();
  GRAPPLE_REQUIRE(startSampleCalled);

  panel.setViewModel(
    viewModelWithCamera(),
    grapple::agent::AgentConversationState{},
    std::nullopt,
    std::nullopt
  );

  GRAPPLE_REQUIRE(panel.suggestedRequestCount() == 3);
  GRAPPLE_REQUIRE(panel.suggestedRequestText(0) == "Center the subject with editable camera controls.");
  panel.triggerSuggestedRequest(0);
  GRAPPLE_REQUIRE(panel.intent() == "Center the subject with editable camera controls.");
  GRAPPLE_REQUIRE(panel.primaryActionText() == "Create Editable Camera Controls");

  auto editedViewModel = viewModelWithCamera();
  editedViewModel.steward.edits.push_back(grapple::app::AppStewardEditRow{
    grapple::foundation::CommandId{"cmd_1"},
    grapple::foundation::RevisionId{"rev_2"},
    grapple::foundation::NodeId{"camera_1"},
    "Camera",
    "Camera Transform",
    "Center subject.",
    "Position X=0.25, Zoom=1.3"
  });
  panel.setViewModel(
    editedViewModel,
    grapple::agent::AgentConversationState{},
    grapple::foundation::NodeId{"camera_1"},
    std::nullopt
  );
  GRAPPLE_REQUIRE(!editSummary->isHidden());
  GRAPPLE_REQUIRE(containsText(editSummary->text().toStdString(), "Editable result: Camera Transform on Camera (rev_2)"));
  GRAPPLE_REQUIRE(containsText(editSummary->text().toStdString(), "Controls: Position X=0.25, Zoom=1.3"));
  GRAPPLE_REQUIRE(containsText(editSummary->text().toStdString(), "Request: Center subject."));

  panel.setIntent("zoom in a little");
  panel.triggerPrimaryAction();
  GRAPPLE_REQUIRE(cameraRouteCalled);
  GRAPPLE_REQUIRE(!textRouteCalled);
  GRAPPLE_REQUIRE(!noteRouteCalled);

  cameraRouteCalled = false;
  textRouteCalled = false;
  noteRouteCalled = false;
  panel.setIntent("add title \"Opening\"");
  panel.triggerPrimaryAction();
  GRAPPLE_REQUIRE(!cameraRouteCalled);
  GRAPPLE_REQUIRE(textRouteCalled);
  GRAPPLE_REQUIRE(!noteRouteCalled);

  return 0;
}
