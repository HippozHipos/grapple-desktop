#pragma once

#include <grapple/app/AppViewModel.hpp>

#include <QWidget>

#include <string>

class QTextEdit;

namespace grapple::desktop {

class StewardPanel final : public QWidget {
public:
  explicit StewardPanel(QWidget* parent = nullptr);

  void setViewModel(const app::AppViewModel& viewModel);
  [[nodiscard]] std::string contents() const;

private:
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::desktop
