#include <grapple/ui_qt/ClipTransformPanel.hpp>

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

ClipTransformPanel::ClipTransformPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("clipTransformPanel");
  layout_ = new QVBoxLayout{this};
  layout_->setContentsMargins(12, 12, 12, 12);
  layout_->setSpacing(10);
}

void ClipTransformPanel::setApplyHandler(ApplyHandler handler) {
  applyHandler_ = std::move(handler);
}

void ClipTransformPanel::setSelection(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  clearControls();
  selectedClipNodeId_ = std::nullopt;

  if (!selectedNodeId.has_value()) {
    addMessage("Select a visual clip to adjust its transform.");
    return;
  }

  const app::AppClipRow* selectedClip = selectedVisualClip(viewModel, selectedNodeId.value());
  if (selectedClip == nullptr) {
    addMessage("Clip transform controls appear when a visual clip is selected.");
    return;
  }

  selectedClipNodeId_ = selectedClip->sourceNodeId;
  auto* title = new QLabel{QString{"Clip Transform - %1"}.arg(qString(selectedClip->assetName))};
  title->setObjectName("clipTransformTitle");
  layout_->addWidget(title);

  positionX_ = addEditor("Position X", "clipTransformPositionX", selectedClip->transform.position.x, -1000.0, 1000.0, 0.05);
  positionY_ = addEditor("Position Y", "clipTransformPositionY", selectedClip->transform.position.y, -1000.0, 1000.0, 0.05);
  scaleX_ = addEditor("Scale X", "clipTransformScaleX", selectedClip->transform.scale.x, 0.01, 1000.0, 0.05);
  scaleY_ = addEditor("Scale Y", "clipTransformScaleY", selectedClip->transform.scale.y, 0.01, 1000.0, 0.05);
  rotation_ = addEditor("Rotation", "clipTransformRotation", selectedClip->transform.rotationDegrees, -3600.0, 3600.0, 1.0);
  opacity_ = addEditor("Opacity", "clipTransformOpacity", selectedClip->transform.opacity, 0.0, 1.0, 0.05);
  playbackRate_ = addEditor("Speed", "clipPlaybackRate", selectedClip->playbackRate, 0.01, 32.0, 0.05);
  layout_->addStretch(1);
}

void ClipTransformPanel::clearControls() {
  selectedClipNodeId_ = std::nullopt;
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

void ClipTransformPanel::addMessage(const QString& message) {
  auto* title = new QLabel{"Clip Transform"};
  title->setObjectName("clipTransformTitle");
  auto* help = new QLabel{message};
  help->setObjectName("clipTransformHelp");
  help->setWordWrap(true);
  layout_->addWidget(title);
  layout_->addWidget(help);
  layout_->addStretch(1);
}

QDoubleSpinBox* ClipTransformPanel::addEditor(
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
  connect(editor, &QDoubleSpinBox::editingFinished, this, [this] { emitCurrentTransform(); });

  rowLayout->addWidget(label);
  rowLayout->addWidget(editor, 1);
  layout_->addWidget(row);
  return editor;
}

void ClipTransformPanel::emitCurrentTransform() {
  if (!selectedClipNodeId_.has_value() ||
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
      playbackRate_->value()
    }
  );
}

} // namespace grapple::ui
