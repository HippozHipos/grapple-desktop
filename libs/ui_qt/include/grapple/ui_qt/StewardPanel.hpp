#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QWidget>

#include <functional>
#include <string>

class QLineEdit;
class QPushButton;
class QTextEdit;

namespace grapple::ui {

class StewardPanel final : public QWidget {
public:
  using CreateCameraEffectHandler = std::function<void(std::string)>;

  explicit StewardPanel(QWidget* parent = nullptr);

  void setCreateCameraEffectHandler(CreateCameraEffectHandler handler);
  void setViewModel(const app::AppViewModel& viewModel);
  void setIntent(std::string intent);
  void triggerCreateCameraEffect();
  [[nodiscard]] std::string contents() const;
  [[nodiscard]] std::string intent() const;

private:
  CreateCameraEffectHandler createCameraEffectHandler_;
  QLineEdit* intent_ = nullptr;
  QPushButton* createCameraEffectButton_ = nullptr;
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::ui
