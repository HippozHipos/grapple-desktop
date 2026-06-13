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
  using CreateCameraEffectHandler = std::function<void(std::string)>;

  explicit StewardPanel(QWidget* parent = nullptr);

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
  CreateCameraEffectHandler createCameraEffectHandler_;
  QTextEdit* intent_ = nullptr;
  QPushButton* createCameraEffectButton_ = nullptr;
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::ui
