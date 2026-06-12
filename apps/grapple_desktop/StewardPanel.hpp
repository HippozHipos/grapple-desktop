#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QWidget>

#include <functional>
#include <string>

class QPushButton;
class QTextEdit;

namespace grapple::desktop {

class StewardPanel final : public QWidget {
public:
  using CreateCameraEffectHandler = std::function<void()>;

  explicit StewardPanel(QWidget* parent = nullptr);

  void setCreateCameraEffectHandler(CreateCameraEffectHandler handler);
  void setViewModel(const app::AppViewModel& viewModel);
  void triggerCreateCameraEffect();
  [[nodiscard]] std::string contents() const;

private:
  CreateCameraEffectHandler createCameraEffectHandler_;
  QPushButton* createCameraEffectButton_ = nullptr;
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::desktop
