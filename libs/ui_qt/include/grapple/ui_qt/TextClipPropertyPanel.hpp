#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QWidget>

#include <functional>
#include <optional>

class QDoubleSpinBox;
class QLineEdit;
class QVBoxLayout;

namespace grapple::ui {

class TextClipPropertyPanel final : public QWidget {
public:
  using ApplyHandler = std::function<void(foundation::NodeId, timeline::TextClipPayload)>;

  explicit TextClipPropertyPanel(QWidget* parent = nullptr);

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
  void emitCurrentEdit();

  QVBoxLayout* layout_ = nullptr;
  ApplyHandler applyHandler_;
  std::optional<foundation::NodeId> selectedClipNodeId_;
  timeline::TextClipStyle selectedStyle_;
  QLineEdit* text_ = nullptr;
  QDoubleSpinBox* timelineStart_ = nullptr;
  QDoubleSpinBox* timelineEnd_ = nullptr;
  QDoubleSpinBox* positionX_ = nullptr;
  QDoubleSpinBox* positionY_ = nullptr;
  QDoubleSpinBox* scaleX_ = nullptr;
  QDoubleSpinBox* scaleY_ = nullptr;
  QDoubleSpinBox* rotation_ = nullptr;
  QDoubleSpinBox* opacity_ = nullptr;
  QDoubleSpinBox* fontSize_ = nullptr;
};

} // namespace grapple::ui
