#include <grapple/ui_qt/ClipPropertyPanel.hpp>

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

const app::AppClipRow* selectedVisualClip(
  const app::AppViewModel& viewModel,
  const foundation::NodeId& selectedNodeId
) {
  const auto selectedClip = std::find_if(
    viewModel.timeline.clips.begin(),
    viewModel.timeline.clips.end(),
    [&](const app::AppClipRow& clip) {
      return clip.sourceNodeId == selectedNodeId;
    }
  );
  return selectedClip == viewModel.timeline.clips.end() ? nullptr : &*selectedClip;
}

} // namespace

ClipPropertyPanel::ClipPropertyPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("clipPropertyPanel");
  layout_ = new QVBoxLayout{this};
  layout_->setContentsMargins(12, 12, 12, 12);
  layout_->setSpacing(10);
}

void ClipPropertyPanel::setApplyHandler(ApplyHandler handler) {
  applyHandler_ = std::move(handler);
}

void ClipPropertyPanel::setSelection(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  clearControls();
  selectedClipNodeId_ = std::nullopt;

  if (!selectedNodeId.has_value()) {
    addMessage("Select a visual clip to adjust its properties.");
    return;
  }

  const app::AppClipRow* selectedClip = selectedVisualClip(viewModel, selectedNodeId.value());
  if (selectedClip == nullptr) {
    addMessage("Clip controls appear when a visual clip is selected.");
    return;
  }

  selectedClipNodeId_ = selectedClip->sourceNodeId;
  auto* title = new QLabel{QString{"Clip - %1"}.arg(qString(selectedClip->assetName))};
  title->setObjectName("clipPropertyTitle");
  layout_->addWidget(title);

  timelineStart_ = addEditor("Timeline Start", "clipTimelineStart", selectedClip->timelineRange.start.value, 0.0, 100000.0, 0.1);
  timelineEnd_ = addEditor("Timeline End", "clipTimelineEnd", selectedClip->timelineRange.end.value, 0.0, 100000.0, 0.1);
  sourceStart_ = addEditor("Source Start", "clipSourceStart", selectedClip->sourceRange.start.value, 0.0, 100000.0, 0.1);
  sourceEnd_ = addEditor("Source End", "clipSourceEnd", selectedClip->sourceRange.end.value, 0.0, 100000.0, 0.1);
  positionX_ = addEditor("Position X", "clipTransformPositionX", selectedClip->transform.position.x, -1000.0, 1000.0, 0.05);
  positionY_ = addEditor("Position Y", "clipTransformPositionY", selectedClip->transform.position.y, -1000.0, 1000.0, 0.05);
  scaleX_ = addEditor("Scale X", "clipTransformScaleX", selectedClip->transform.scale.x, 0.01, 1000.0, 0.05);
  scaleY_ = addEditor("Scale Y", "clipTransformScaleY", selectedClip->transform.scale.y, 0.01, 1000.0, 0.05);
  rotation_ = addEditor("Rotation", "clipTransformRotation", selectedClip->transform.rotationDegrees, -3600.0, 3600.0, 1.0);
  opacity_ = addEditor("Opacity", "clipTransformOpacity", selectedClip->transform.opacity, 0.0, 1.0, 0.05);
  playbackRate_ = addEditor("Speed", "clipPlaybackRate", selectedClip->playbackRate, 0.01, 32.0, 0.05);
  layout_->addStretch(1);
}

void ClipPropertyPanel::clearControls() {
  selectedClipNodeId_ = std::nullopt;
  timelineStart_ = nullptr;
  timelineEnd_ = nullptr;
  sourceStart_ = nullptr;
  sourceEnd_ = nullptr;
  positionX_ = nullptr;
  positionY_ = nullptr;
  scaleX_ = nullptr;
  scaleY_ = nullptr;
  rotation_ = nullptr;
  opacity_ = nullptr;
  playbackRate_ = nullptr;

  while (QLayoutItem* item = layout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

void ClipPropertyPanel::addMessage(const QString& message) {
  auto* title = new QLabel{"Clip"};
  title->setObjectName("clipPropertyTitle");
  auto* help = new QLabel{message};
  help->setObjectName("clipPropertyHelp");
  help->setWordWrap(true);
  layout_->addWidget(title);
  layout_->addWidget(help);
  layout_->addStretch(1);
}

QDoubleSpinBox* ClipPropertyPanel::addEditor(
  const QString& labelText,
  const QString& objectName,
  double value,
  double minimum,
  double maximum,
  double step
) {
  auto* row = new QWidget;
  auto* rowLayout = new QHBoxLayout{row};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(8);

  auto* label = new QLabel{labelText};
  label->setMinimumWidth(82);

  auto* editor = new QDoubleSpinBox;
  editor->setObjectName(objectName);
  editor->setRange(minimum, maximum);
  editor->setDecimals(4);
  editor->setSingleStep(step);
  editor->setKeyboardTracking(false);
  editor->setValue(value);
  connect(editor, &QDoubleSpinBox::editingFinished, this, [this] { emitCurrentEdit(); });

  rowLayout->addWidget(label);
  rowLayout->addWidget(editor, 1);
  layout_->addWidget(row);
  return editor;
}

void ClipPropertyPanel::emitCurrentEdit() {
  if (!selectedClipNodeId_.has_value() ||
      timelineStart_ == nullptr ||
      timelineEnd_ == nullptr ||
      sourceStart_ == nullptr ||
      sourceEnd_ == nullptr ||
      positionX_ == nullptr ||
      positionY_ == nullptr ||
      scaleX_ == nullptr ||
      scaleY_ == nullptr ||
      rotation_ == nullptr ||
      opacity_ == nullptr ||
      playbackRate_ == nullptr ||
      !applyHandler_) {
    return;
  }

  applyHandler_(
    selectedClipNodeId_.value(),
    ClipEdit{
      foundation::Transform2D{
        foundation::Vec2{positionX_->value(), positionY_->value()},
        foundation::Vec2{scaleX_->value(), scaleY_->value()},
        rotation_->value(),
        opacity_->value()
      },
      foundation::TimeRange{
        foundation::TimeSeconds{timelineStart_->value()},
        foundation::TimeSeconds{timelineEnd_->value()}
      },
      foundation::TimeRange{
        foundation::TimeSeconds{sourceStart_->value()},
        foundation::TimeSeconds{sourceEnd_->value()}
      },
      playbackRate_->value()
    }
  );
}

} // namespace grapple::ui
