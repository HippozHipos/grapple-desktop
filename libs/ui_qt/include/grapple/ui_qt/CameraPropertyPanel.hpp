#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QDoubleSpinBox;
class QLineEdit;
class QVBoxLayout;

namespace grapple::ui {

class CameraPropertyPanel final : public QWidget {
public:
  using ApplyHandler = std::function<void(foundation::NodeId, std::string, double)>;

  explicit CameraPropertyPanel(QWidget* parent = nullptr);

  void setApplyHandler(ApplyHandler handler);
  void setSelection(
    const app::AppViewModel& viewModel,
    const std::optional<foundation::NodeId>& selectedNodeId
  );

private:
  void clearControls();
  void addMessage(const QString& message);
  void emitCurrentCamera();

  QVBoxLayout* layout_ = nullptr;
  ApplyHandler applyHandler_;
  std::optional<foundation::NodeId> selectedCameraNodeId_;
  QLineEdit* name_ = nullptr;
  QDoubleSpinBox* focalLength_ = nullptr;
};

} // namespace grapple::ui
