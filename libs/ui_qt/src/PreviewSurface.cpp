#include <grapple/ui_qt/PreviewSurface.hpp>

#include <QColor>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPaintEvent>
#include <QStringList>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

QString timeText(foundation::TimeSeconds time) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << time.value << "s";
  return qString(output.str());
}

QString mediaKindText(render::RenderedMediaKind kind) {
  switch (kind) {
    case render::RenderedMediaKind::Video:
      return "Video";
    case render::RenderedMediaKind::Image:
      return "Image";
  }

  return "Unknown";
}

QRect scaledRect(const QSize& sourceSize, const QRect& bounds) {
  const double scale = std::min(
    static_cast<double>(bounds.width()) / static_cast<double>(sourceSize.width()),
    static_cast<double>(bounds.height()) / static_cast<double>(sourceSize.height())
  );
  const int width = static_cast<int>(static_cast<double>(sourceSize.width()) * scale);
  const int height = static_cast<int>(static_cast<double>(sourceSize.height()) * scale);
  return QRect{
    bounds.left() + ((bounds.width() - width) / 2),
    bounds.top() + ((bounds.height() - height) / 2),
    width,
    height
  };
}

} // namespace

PreviewSurface::PreviewSurface(QWidget* parent)
  : QWidget{parent} {
  setObjectName("previewSurface");
  setMinimumSize(480, 280);
}

void PreviewSurface::setAssetLabels(const app::AppAssetSummary& assets) {
  assetLabels_.clear();
  assetLabels_.reserve(assets.rows.size());
  for (const app::AppAssetRow& asset : assets.rows) {
    assetLabels_.push_back({asset.assetId, asset.name});
  }
  update();
}

void PreviewSurface::setFrame(std::shared_ptr<const render::RenderFrame> frame) {
  frame_ = std::move(frame);
  update();
}

void PreviewSurface::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);

  QPainter painter{this};
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), QColor{"#0e1118"});

  if (frame_ == nullptr) {
    drawCenteredText(painter, "No frame rendered");
    return;
  }

  const render::RenderFrame& frame = *frame_;
  if (frame.image.has_value()) {
    drawRenderedImage(painter, frame);
    return;
  }

  if (frame.mediaFrames.empty()) {
    drawCenteredText(painter, QString{"%1\nNo active media at playhead"}.arg(timeText(frame.time)));
    return;
  }

  const QRect canvas = rect().adjusted(36, 34, -36, -34);
  painter.setPen(QColor{"#3e536e"});
  painter.setBrush(QColor{"#151b25"});
  painter.drawRoundedRect(canvas, 14, 14);

  const int stackOffset = 18;
  int index = 0;
  for (const render::RenderedMediaFrame& mediaFrame : frame.mediaFrames) {
    const QRect card = canvas.adjusted(
      28 + (index * stackOffset),
      28 + (index * stackOffset),
      -28 + (index * stackOffset),
      -28 + (index * stackOffset)
    );
    drawMediaFrame(painter, mediaFrame, card);
    ++index;
    if (index == 3) {
      break;
    }
  }

  painter.setPen(QColor{"#d8f3ff"});
  painter.setFont(QFont{"DejaVu Sans", 13, QFont::Bold});
  painter.drawText(rect().adjusted(18, 10, -18, -10), Qt::AlignTop | Qt::AlignHCenter, timeText(frame.time));

  if (frame.mediaFrames.size() > 3) {
    painter.setPen(QColor{"#9fb7d5"});
    painter.setFont(QFont{"DejaVu Sans", 10});
    painter.drawText(rect().adjusted(18, -34, -18, -12), Qt::AlignBottom | Qt::AlignHCenter, QString{"+%1 more active clips"}.arg(frame.mediaFrames.size() - 3));
  }
}

QString PreviewSurface::elidedText(QPainter& painter, const QString& text, int width) {
  return QFontMetrics{painter.font()}.elidedText(text, Qt::ElideRight, std::max(12, width));
}

void PreviewSurface::drawCenteredText(QPainter& painter, const QString& text) const {
  painter.setPen(QColor{"#d8f3ff"});
  painter.setFont(QFont{"DejaVu Sans", 16, QFont::Bold});
  painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter, text);
}

void PreviewSurface::drawRenderedImage(QPainter& painter, const render::RenderFrame& frame) const {
  const render::RenderedImage& image = frame.image.value();
  const int expectedBytes = image.resolution.width * image.resolution.height * 4;
  if (image.resolution.width <= 0 ||
      image.resolution.height <= 0 ||
      static_cast<int>(image.rgbaPixels.size()) != expectedBytes) {
    drawCenteredText(painter, "Invalid rendered image");
    return;
  }

  const QImage qImage{
    image.rgbaPixels.data(),
    image.resolution.width,
    image.resolution.height,
    image.resolution.width * 4,
    QImage::Format_RGBA8888
  };
  const QRect target = scaledRect(qImage.size(), rect().adjusted(30, 30, -30, -30));
  painter.drawImage(target, qImage);

  painter.setPen(QColor{"#d8f3ff"});
  painter.setFont(QFont{"DejaVu Sans", 13, QFont::Bold});
  painter.drawText(rect().adjusted(18, 10, -18, -10), Qt::AlignTop | Qt::AlignHCenter, timeText(frame.time));

  if (!frame.mediaFrames.empty()) {
    painter.setPen(QColor{"#e8f4ff"});
    painter.setFont(QFont{"DejaVu Sans", 10});
    const render::RenderedMediaFrame& mediaFrame = frame.mediaFrames.front();
    painter.drawText(
      rect().adjusted(18, -32, -18, -10),
      Qt::AlignBottom | Qt::AlignHCenter,
      QString{"%1  %2"}.arg(assetLabel(mediaFrame.assetId)).arg(timeText(mediaFrame.sourceTime))
    );
  }
}

void PreviewSurface::drawMediaFrame(
  QPainter& painter,
  const render::RenderedMediaFrame& mediaFrame,
  const QRect& card
) const {
  painter.setPen(QColor{"#b9c7f0"});
  painter.setBrush(QColor{"#30436a"});
  painter.drawRoundedRect(card, 12, 12);

  painter.setPen(QColor{"#edf5ff"});
  painter.setFont(QFont{"DejaVu Sans", 18, QFont::Bold});
  painter.drawText(card.adjusted(18, 18, -18, -18), Qt::AlignTop | Qt::AlignLeft, mediaKindText(mediaFrame.kind));

  painter.setFont(QFont{"DejaVu Sans", 12});
  const QRect detailsRect = card.adjusted(18, 58, -18, -18);
  const QStringList lines{
    QString{"asset %1"}.arg(assetLabel(mediaFrame.assetId)),
    QString{"clip %1"}.arg(qString(mediaFrame.clipNodeId.value())),
    QString{"source %1"}.arg(timeText(mediaFrame.sourceTime))
  };
  int y = detailsRect.top();
  for (const QString& line : lines) {
    painter.drawText(detailsRect.left(), y + 18, elidedText(painter, line, detailsRect.width()));
    y += 24;
  }
}

QString PreviewSurface::assetLabel(const foundation::AssetId& assetId) const {
  for (const auto& assetLabel : assetLabels_) {
    if (assetLabel.first == assetId) {
      return qString(assetLabel.second);
    }
  }
  return qString(assetId.value());
}

} // namespace grapple::ui
