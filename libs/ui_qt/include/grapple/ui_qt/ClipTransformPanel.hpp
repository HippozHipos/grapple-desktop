#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/foundation/Transform.hpp>

#include <QWidget>

#include <functional>
#include <optional>

class QDoubleSpinBox;
class QVBoxLayout;

namespace grapple::ui {

class ClipTransformPanel final : public QWidget {
public:
  struct ClipEdit {
    foundation::Transform2D transform;
    double playbackRate = 1.0;
  };

  using ApplyHandler = std::function<void(foundation::NodeId, ClipEdit)>;

  explicit ClipTransformPanel(QWidget* parent = nullptr);

  void setApplyHandler(ApplyHandler handler);
  void setSelection(
    const app::AppViewModel& viewModel,
    const std::optional<foundation::NodeId>& selectedNodeId
  );

private:
  void clearControls();
  void addMessage(const QString& message);
  QDoubleSpinBox* addEditor(
    const QString& labelText,
    const QString& objectName,
    double value,
    double minimum,
    double maximum,
    double step
  );
  void emitCurrentTransform();

  QVBoxLayout* layout_ = nullptr;
  ApplyHandler applyHandler_;
  std::optional<foundation::NodeId> selectedClipNodeId_;
  QDoubleSpinBox* positionX_ = nullptr;
  QDoubleSpinBox* positionY_ = nullptr;
  QDoubleSpinBox* scaleX_ = nullptr;
  QDoubleSpinBox* scaleY_ = nullptr;
  QDoubleSpinBox* rotation_ = nullptr;
  QDoubleSpinBox* opacity_ = nullptr;
  QDoubleSpinBox* playbackRate_ = nullptr;
};

} // namespace grapple::ui
