#include <grapple/ui_qt/TextClipPropertyPanel.hpp>

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

const app::AppTextClipRow* selectedTextClip(
  const app::AppViewModel& viewModel,
  const foundation::NodeId& selectedNodeId
) {
  const auto selectedClip = std::find_if(
    viewModel.timeline.textClips.begin(),
    viewModel.timeline.textClips.end(),
    [&](const app::AppTextClipRow& clip) {
      return clip.sourceNodeId == selectedNodeId;
    }
  );
  return selectedClip == viewModel.timeline.textClips.end() ? nullptr : &*selectedClip;
}

} // namespace

TextClipPropertyPanel::TextClipPropertyPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("textClipPropertyPanel");
  layout_ = new QVBoxLayout{this};
  layout_->setContentsMargins(12, 12, 12, 12);
  layout_->setSpacing(10);
}

void TextClipPropertyPanel::setApplyHandler(ApplyHandler handler) {
  applyHandler_ = std::move(handler);
}

void TextClipPropertyPanel::setSelection(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  clearControls();
  selectedClipNodeId_ = std::nullopt;

  if (!selectedNodeId.has_value()) {
    addMessage("Select a text clip to edit its text and placement.");
    return;
  }

  const app::AppTextClipRow* selectedClip = selectedTextClip(viewModel, selectedNodeId.value());
  if (selectedClip == nullptr) {
    addMessage("Text controls appear when a text clip is selected.");
    return;
  }

  selectedClipNodeId_ = selectedClip->sourceNodeId;
  selectedStyle_ = selectedClip->style;

  auto* title = new QLabel{"Text Clip"};
  title->setObjectName("textClipPropertyTitle");
  layout_->addWidget(title);

  text_ = new QLineEdit{qString(selectedClip->text)};
  text_->setObjectName("textClipText");
  connect(text_, &QLineEdit::editingFinished, this, [this] { emitCurrentEdit(); });
  layout_->addWidget(text_);

  timelineStart_ = addEditor("Timeline Start", "textClipTimelineStart", selectedClip->timelineRange.start.value, 0.0, 100000.0, 0.1);
  timelineEnd_ = addEditor("Timeline End", "textClipTimelineEnd", selectedClip->timelineRange.end.value, 0.0, 100000.0, 0.1);
  positionX_ = addEditor("Position X", "textClipPositionX", selectedClip->transform.position.x, -1000.0, 1000.0, 0.05);
  positionY_ = addEditor("Position Y", "textClipPositionY", selectedClip->transform.position.y, -1000.0, 1000.0, 0.05);
  scaleX_ = addEditor("Scale X", "textClipScaleX", selectedClip->transform.scale.x, 0.01, 1000.0, 0.05);
  scaleY_ = addEditor("Scale Y", "textClipScaleY", selectedClip->transform.scale.y, 0.01, 1000.0, 0.05);
  rotation_ = addEditor("Rotation", "textClipRotation", selectedClip->transform.rotationDegrees, -3600.0, 3600.0, 1.0);
  opacity_ = addEditor("Opacity", "textClipOpacity", selectedClip->transform.opacity, 0.0, 1.0, 0.05);
  fontSize_ = addEditor("Font Size", "textClipFontSize", selectedClip->style.fontSize, 1.0, 1000.0, 1.0);
  layout_->addStretch(1);
}

void TextClipPropertyPanel::clearControls() {
  selectedClipNodeId_ = std::nullopt;
  selectedStyle_ = timeline::TextClipStyle{};
  text_ = nullptr;
  timelineStart_ = nullptr;
  timelineEnd_ = nullptr;
  positionX_ = nullptr;
  positionY_ = nullptr;
  scaleX_ = nullptr;
  scaleY_ = nullptr;
  rotation_ = nullptr;
  opacity_ = nullptr;
  fontSize_ = nullptr;

  while (QLayoutItem* item = layout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

void TextClipPropertyPanel::addMessage(const QString& message) {
  auto* title = new QLabel{"Text Clip"};
  title->setObjectName("textClipPropertyTitle");
  auto* help = new QLabel{message};
  help->setObjectName("textClipPropertyHelp");
  help->setWordWrap(true);
  layout_->addWidget(title);
  layout_->addWidget(help);
  layout_->addStretch(1);
}

QDoubleSpinBox* TextClipPropertyPanel::addEditor(
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
  label->setMinimumWidth(92);

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

void TextClipPropertyPanel::emitCurrentEdit() {
  if (!selectedClipNodeId_.has_value() ||
      text_ == nullptr ||
      timelineStart_ == nullptr ||
      timelineEnd_ == nullptr ||
      positionX_ == nullptr ||
      positionY_ == nullptr ||
      scaleX_ == nullptr ||
      scaleY_ == nullptr ||
      rotation_ == nullptr ||
      opacity_ == nullptr ||
      fontSize_ == nullptr ||
      !applyHandler_) {
    return;
  }

  timeline::TextClipStyle style = selectedStyle_;
  style.fontSize = fontSize_->value();
  applyHandler_(
    selectedClipNodeId_.value(),
    timeline::TextClipPayload{
      text_->text().toStdString(),
      foundation::TimeRange{
        foundation::TimeSeconds{timelineStart_->value()},
        foundation::TimeSeconds{timelineEnd_->value()}
      },
      timeline::Transform2D{
        foundation::Vec2{positionX_->value(), positionY_->value()},
        foundation::Vec2{scaleX_->value(), scaleY_->value()},
        rotation_->value(),
        opacity_->value()
      },
      style
    }
  );
}

} // namespace grapple::ui
