#include <grapple/ui_qt/CameraPropertyPanel.hpp>

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

const app::AppCameraRow* selectedCamera(
  const app::AppViewModel& viewModel,
  const foundation::NodeId& selectedNodeId
) {
  const auto camera = std::find_if(
    viewModel.timeline.cameras.begin(),
    viewModel.timeline.cameras.end(),
    [&](const app::AppCameraRow& row) {
      return row.sourceNodeId == selectedNodeId;
    }
  );
  return camera == viewModel.timeline.cameras.end() ? nullptr : &*camera;
}

} // namespace

CameraPropertyPanel::CameraPropertyPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("cameraPropertyPanel");
  layout_ = new QVBoxLayout{this};
  layout_->setContentsMargins(12, 12, 12, 12);
  layout_->setSpacing(10);
}

void CameraPropertyPanel::setApplyHandler(ApplyHandler handler) {
  applyHandler_ = std::move(handler);
}

void CameraPropertyPanel::setSelection(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  clearControls();
  selectedCameraNodeId_ = std::nullopt;

  if (!selectedNodeId.has_value()) {
    addMessage("Select a camera to edit its properties.");
    return;
  }

  const app::AppCameraRow* camera = selectedCamera(viewModel, selectedNodeId.value());
  if (camera == nullptr) {
    addMessage("Camera controls appear when a camera is selected.");
    return;
  }

  selectedCameraNodeId_ = camera->sourceNodeId;
  auto* title = new QLabel{"Camera Properties"};
  title->setObjectName("cameraPropertyTitle");
  layout_->addWidget(title);

  auto* nameRow = new QWidget;
  auto* nameLayout = new QHBoxLayout{nameRow};
  nameLayout->setContentsMargins(0, 0, 0, 0);
  nameLayout->setSpacing(8);
  auto* nameLabel = new QLabel{"Name"};
  nameLabel->setMinimumWidth(92);
  name_ = new QLineEdit{qString(camera->name)};
  name_->setObjectName("cameraPropertyName");
  connect(name_, &QLineEdit::editingFinished, this, [this] { emitCurrentCamera(); });
  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(name_, 1);
  layout_->addWidget(nameRow);

  auto* focalLengthRow = new QWidget;
  auto* focalLengthLayout = new QHBoxLayout{focalLengthRow};
  focalLengthLayout->setContentsMargins(0, 0, 0, 0);
  focalLengthLayout->setSpacing(8);
  auto* focalLengthLabel = new QLabel{"Focal Length"};
  focalLengthLabel->setMinimumWidth(92);
  focalLength_ = new QDoubleSpinBox;
  focalLength_->setObjectName("cameraPropertyFocalLength");
  focalLength_->setRange(1.0, 1000.0);
  focalLength_->setDecimals(1);
  focalLength_->setSingleStep(1.0);
  focalLength_->setKeyboardTracking(false);
  focalLength_->setValue(camera->lens.focalLength);
  connect(focalLength_, &QDoubleSpinBox::editingFinished, this, [this] { emitCurrentCamera(); });
  focalLengthLayout->addWidget(focalLengthLabel);
  focalLengthLayout->addWidget(focalLength_, 1);
  layout_->addWidget(focalLengthRow);
  layout_->addStretch(1);
}

void CameraPropertyPanel::clearControls() {
  selectedCameraNodeId_ = std::nullopt;
  name_ = nullptr;
  focalLength_ = nullptr;

  while (QLayoutItem* item = layout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

void CameraPropertyPanel::addMessage(const QString& message) {
  auto* title = new QLabel{"Camera Properties"};
  title->setObjectName("cameraPropertyTitle");
  auto* help = new QLabel{message};
  help->setObjectName("cameraPropertyHelp");
  help->setWordWrap(true);
  layout_->addWidget(title);
  layout_->addWidget(help);
  layout_->addStretch(1);
}

void CameraPropertyPanel::emitCurrentCamera() {
  if (!selectedCameraNodeId_.has_value() ||
      name_ == nullptr ||
      focalLength_ == nullptr ||
      !applyHandler_) {
    return;
  }

  applyHandler_(
    selectedCameraNodeId_.value(),
    name_->text().toStdString(),
    focalLength_->value()
  );
}

} // namespace grapple::ui
