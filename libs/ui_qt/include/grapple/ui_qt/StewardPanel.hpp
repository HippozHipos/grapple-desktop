#pragma once

#include <grapple/agent/AgentConversationState.hpp>
#include <grapple/app/AppViewModel.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QPushButton;
class QTextEdit;

namespace grapple::ui {

class StewardPanel final : public QWidget {
public:
  using AddCameraHandler = std::function<void()>;
  using ShowCameraControlsHandler = std::function<void(foundation::NodeId)>;
  using CreateCameraEffectHandler = std::function<void(std::string)>;

  explicit StewardPanel(QWidget* parent = nullptr);

  void setAddCameraHandler(AddCameraHandler handler);
  void setShowCameraControlsHandler(ShowCameraControlsHandler handler);
  void setCreateCameraEffectHandler(CreateCameraEffectHandler handler);
  void setViewModel(
    const app::AppViewModel& viewModel,
    const agent::AgentConversationState& conversationState,
    const std::optional<foundation::NodeId>& selectedNodeId
  );
  void setIntent(std::string intent);
  void triggerCreateCameraEffect();
  [[nodiscard]] std::string contents() const;
  [[nodiscard]] std::string intent() const;

private:
  enum class PrimaryAction {
    Disabled,
    AddCamera,
    ShowCameraControls,
    CreateCameraEffect
  };

  AddCameraHandler addCameraHandler_;
  ShowCameraControlsHandler showCameraControlsHandler_;
  CreateCameraEffectHandler createCameraEffectHandler_;
  PrimaryAction primaryAction_ = PrimaryAction::Disabled;
  std::optional<foundation::NodeId> primaryTargetCameraNodeId_;
  QTextEdit* intent_ = nullptr;
  QPushButton* createCameraEffectButton_ = nullptr;
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::ui
