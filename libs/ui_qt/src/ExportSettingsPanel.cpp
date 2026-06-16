#include <grapple/ui_qt/ExportSettingsPanel.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace grapple::ui {

namespace {

constexpr int DefaultWidth = 1920;
constexpr int DefaultHeight = 1080;
constexpr double DefaultFramesPerSecond = 30.0;

} // namespace

ExportSettingsPanel::ExportSettingsPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("exportSettingsPanel");
  layout_ = new QVBoxLayout{this};
  layout_->setContentsMargins(12, 12, 12, 12);
  layout_->setSpacing(10);

  auto* title = new QLabel{"Export Settings"};
  title->setObjectName("exportSettingsTitle");
  layout_->addWidget(title);

  width_ = addIntegerEditor("Width", "exportSettingsWidth", DefaultWidth, 16, 16384);
  height_ = addIntegerEditor("Height", "exportSettingsHeight", DefaultHeight, 16, 16384);

  auto* fpsRow = new QWidget;
  auto* fpsLayout = new QHBoxLayout{fpsRow};
  fpsLayout->setContentsMargins(0, 0, 0, 0);
  fpsLayout->setSpacing(8);
  auto* fpsLabel = new QLabel{"FPS"};
  fpsLabel->setMinimumWidth(92);
  framesPerSecond_ = new QDoubleSpinBox;
  framesPerSecond_->setObjectName("exportSettingsFps");
  framesPerSecond_->setRange(1.0, 240.0);
  framesPerSecond_->setDecimals(3);
  framesPerSecond_->setSingleStep(1.0);
  framesPerSecond_->setKeyboardTracking(false);
  framesPerSecond_->setValue(DefaultFramesPerSecond);
  connect(framesPerSecond_, &QDoubleSpinBox::editingFinished, this, [this] { emitDraft(); });
  fpsLayout->addWidget(fpsLabel);
  fpsLayout->addWidget(framesPerSecond_, 1);
  layout_->addWidget(fpsRow);

  auto* codecRow = new QWidget;
  auto* codecLayout = new QHBoxLayout{codecRow};
  codecLayout->setContentsMargins(0, 0, 0, 0);
  codecLayout->setSpacing(8);
  auto* codecLabel = new QLabel{"Codec"};
  codecLabel->setMinimumWidth(92);
  codec_ = new QComboBox;
  codec_->setObjectName("exportSettingsCodec");
  codec_->addItem("mp4v");
  codec_->addItem("mjpeg");
  connect(codec_, &QComboBox::currentTextChanged, this, [this] { emitDraft(); });
  codecLayout->addWidget(codecLabel);
  codecLayout->addWidget(codec_, 1);
  layout_->addWidget(codecRow);

  status_ = new QLabel{"No export yet"};
  status_->setObjectName("exportSettingsStatus");
  status_->setWordWrap(true);
  layout_->addWidget(status_);

  layout_->addStretch(1);
}

void ExportSettingsPanel::setApplyHandler(ApplyHandler handler) {
  applyHandler_ = std::move(handler);
}

ExportSettingsDraft ExportSettingsPanel::draft() const {
  const int fpsNumerator = static_cast<int>(std::lround(framesPerSecond_->value() * 1000.0));
  return ExportSettingsDraft{
    foundation::Resolution{width_->value(), height_->value()},
    foundation::FrameRate{fpsNumerator, 1000},
    render::Codec{codec_->currentText().toStdString()}
  };
}

void ExportSettingsPanel::setStatus(std::string status) {
  status_->setText(QString::fromStdString(std::move(status)));
}

std::string ExportSettingsPanel::status() const {
  return status_->text().toStdString();
}

QSpinBox* ExportSettingsPanel::addIntegerEditor(
  const QString& labelText,
  const QString& objectName,
  int value,
  int minimum,
  int maximum
) {
  auto* row = new QWidget;
  auto* rowLayout = new QHBoxLayout{row};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(8);

  auto* label = new QLabel{labelText};
  label->setMinimumWidth(92);

  auto* editor = new QSpinBox;
  editor->setObjectName(objectName);
  editor->setRange(minimum, maximum);
  editor->setKeyboardTracking(false);
  editor->setValue(value);
  connect(editor, &QSpinBox::editingFinished, this, [this] { emitDraft(); });

  rowLayout->addWidget(label);
  rowLayout->addWidget(editor, 1);
  layout_->addWidget(row);
  return editor;
}

void ExportSettingsPanel::emitDraft() {
  if (applyHandler_) {
    applyHandler_(draft());
  }
}

} // namespace grapple::ui
