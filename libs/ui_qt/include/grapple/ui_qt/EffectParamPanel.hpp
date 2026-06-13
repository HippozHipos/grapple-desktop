#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QVBoxLayout;

namespace grapple::ui {

class EffectParamPanel final : public QWidget {
public:
  using ApplyHandler = std::function<void(foundation::NodeId, std::string, double)>;
  using DeleteHandler = std::function<void(foundation::NodeId)>;

  explicit EffectParamPanel(QWidget* parent = nullptr);

  void setApplyHandler(ApplyHandler handler);
  void setDeleteHandler(DeleteHandler handler);
  void setSelection(
    const app::AppViewModel& viewModel,
    const std::optional<foundation::NodeId>& selectedNodeId
  );

private:
  void clearControls();
  void addMessage(const QString& message);

  QVBoxLayout* layout_ = nullptr;
  ApplyHandler applyHandler_;
  DeleteHandler deleteHandler_;
};

} // namespace grapple::ui
