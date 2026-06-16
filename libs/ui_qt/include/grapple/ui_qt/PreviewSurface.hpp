#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/render/RenderFrame.hpp>

#include <QRect>
#include <QString>
#include <QWidget>

#include <memory>
#include <string>
#include <utility>
#include <vector>

class QPaintEvent;
class QPainter;

namespace grapple::ui {

class PreviewSurface final : public QWidget {
public:
  explicit PreviewSurface(QWidget* parent = nullptr);

  void setAssetLabels(const app::AppAssetSummary& assets);
  void setFrame(std::shared_ptr<const render::RenderFrame> frame);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  static QString elidedText(QPainter& painter, const QString& text, int width);
  void drawCenteredText(QPainter& painter, const QString& text) const;
  void drawRenderedImage(QPainter& painter, const render::RenderFrame& frame) const;
  void drawMediaFrame(QPainter& painter, const render::RenderedMediaFrame& mediaFrame, const QRect& card) const;
  void drawTextFrame(QPainter& painter, const render::RenderedTextFrame& textFrame, const QRect& bounds) const;
  void updateFrameToolTip();
  [[nodiscard]] QString assetLabel(const foundation::AssetId& assetId) const;

  std::shared_ptr<const render::RenderFrame> frame_;
  std::vector<std::pair<foundation::AssetId, std::string>> assetLabels_;
};

} // namespace grapple::ui
