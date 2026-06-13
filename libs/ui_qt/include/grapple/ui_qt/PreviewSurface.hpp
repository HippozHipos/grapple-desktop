#pragma once

#include <grapple/render/LocalRenderCore.hpp>

#include <QRect>
#include <QString>
#include <QWidget>

#include <optional>

class QPaintEvent;
class QPainter;

namespace grapple::ui {

class PreviewSurface final : public QWidget {
public:
  explicit PreviewSurface(QWidget* parent = nullptr);

  void setFrame(render::RenderFrame frame);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  static QString elidedText(QPainter& painter, const QString& text, int width);
  void drawCenteredText(QPainter& painter, const QString& text) const;
  void drawRenderedImage(QPainter& painter, const render::RenderFrame& frame) const;
  void drawMediaFrame(QPainter& painter, const render::RenderedMediaFrame& mediaFrame, const QRect& card) const;

  std::optional<render::RenderFrame> frame_;
};

} // namespace grapple::ui
