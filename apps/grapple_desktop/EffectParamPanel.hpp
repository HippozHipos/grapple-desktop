#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QVBoxLayout;

namespace grapple::desktop {

class EffectParamPanel final : public QWidget {
public:
  using ApplyHandler = std::function<void(foundation::NodeId, std::string, double)>;

  explicit EffectParamPanel(QWidget* parent = nullptr);

  void setApplyHandler(ApplyHandler handler);
  void setSelection(
    const app::AppViewModel& viewModel,
    const std::optional<foundation::NodeId>& selectedNodeId
  );

private:
  void clearControls();
  void addMessage(const QString& message);

  QVBoxLayout* layout_ = nullptr;
  ApplyHandler applyHandler_;
};

} // namespace grapple::desktop
