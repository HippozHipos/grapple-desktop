#include <grapple/ui_qt/CompositionViewport.hpp>

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

double positiveScale(double value) {
  return std::max(0.05, std::abs(value));
}

} // namespace

CompositionViewport::CompositionViewport(QWidget* parent)
  : QWidget{parent} {
  setObjectName("compositionViewport");
  setMinimumSize(360, 280);
}

void CompositionViewport::setViewModel(app::AppViewModel viewModel) {
  viewModel_ = std::move(viewModel);
  update();
}

void CompositionViewport::setFrame(std::shared_ptr<const render::RenderFrame> frame) {
  frame_ = std::move(frame);
  if (frame_ != nullptr) {
    playhead_ = frame_->time;
  }
  update();
}

void CompositionViewport::setPlayhead(foundation::TimeSeconds playhead) {
  playhead_ = playhead;
  update();
}

void CompositionViewport::setSelectedNodeId(std::optional<foundation::NodeId> selectedNodeId) {
  selectedNodeId_ = std::move(selectedNodeId);
  update();
}

void CompositionViewport::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);

  QPainter painter{this};
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), QColor{"#10151f"});

  if (!viewModel_.has_value()) {
    painter.setPen(QColor{"#d8f3ff"});
    painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter, "No composition loaded");
    return;
  }

  const QRectF world = QRectF{rect()}.adjusted(30.0, 24.0, -30.0, -30.0);
  painter.setPen(QPen{QColor{"#2e3a4d"}, 1});
  painter.setBrush(QColor{"#151c28"});
  painter.drawRoundedRect(world, 12.0, 12.0);
  drawGrid(painter, world);

  if (frame_ != nullptr && frame_->time == playhead_) {
    for (const render::RenderedMediaFrame& mediaFrame : frame_->mediaFrames) {
      drawMediaFrame(painter, mediaFrame, world);
    }
    for (const render::RenderedCamera& camera : frame_->cameras) {
      drawCamera(painter, camera, world);
    }
  }

  painter.setPen(QColor{"#7890ad"});
  painter.setFont(QFont{"DejaVu Sans", 9});
  painter.drawText(
    world.adjusted(10.0, 8.0, -10.0, -8.0),
    Qt::AlignTop | Qt::AlignRight,
    QString{"%1s"}.arg(playhead_.value, 0, 'f', 2)
  );
}

void CompositionViewport::drawGrid(QPainter& painter, const QRectF& world) const {
  painter.setPen(QPen{QColor{"#223044"}, 1});
  constexpr int divisions = 8;
  for (int index = 1; index < divisions; ++index) {
    const double x = world.left() + (world.width() * static_cast<double>(index) / divisions);
    const double y = world.top() + (world.height() * static_cast<double>(index) / divisions);
    painter.drawLine(QPointF{x, world.top()}, QPointF{x, world.bottom()});
    painter.drawLine(QPointF{world.left(), y}, QPointF{world.right(), y});
  }

  painter.setPen(QPen{QColor{"#36587c"}, 1});
  painter.drawLine(QPointF{world.center().x(), world.top()}, QPointF{world.center().x(), world.bottom()});
  painter.drawLine(QPointF{world.left(), world.center().y()}, QPointF{world.right(), world.center().y()});
}

void CompositionViewport::drawMediaFrame(
  QPainter& painter,
  const render::RenderedMediaFrame& mediaFrame,
  const QRectF& world
) const {
  double aspect = 16.0 / 9.0;
  if (const auto dimensions = dimensionsFor(mediaFrame.assetId); dimensions.has_value() && dimensions->height > 0) {
    aspect = static_cast<double>(dimensions->width) / static_cast<double>(dimensions->height);
  }

  const double baseHeight = 1.45;
  QRectF clipRect = worldRect(baseHeight * aspect, baseHeight, mediaFrame.transform, world);
  const bool isSelected = selected(mediaFrame.clipNodeId);

  painter.save();
  painter.translate(clipRect.center());
  painter.rotate(mediaFrame.transform.rotationDegrees);
  clipRect.moveCenter(QPointF{0.0, 0.0});

  painter.setPen(isSelected ? QPen{QColor{"#f4fbff"}, 4} : QPen{QColor{"#6ea5ff"}, 2});
  painter.setBrush(QColor{isSelected ? "#29446e" : "#213354"});
  painter.drawRoundedRect(clipRect, 8.0, 8.0);

  painter.setPen(QColor{"#eaf3ff"});
  painter.setFont(QFont{"DejaVu Sans", 9, QFont::Bold});
  const QString label = QFontMetrics{painter.font()}.elidedText(
    qString(mediaFrameLabel(mediaFrame)),
    Qt::ElideRight,
    static_cast<int>(std::max(20.0, clipRect.width() - 16.0))
  );
  painter.drawText(clipRect.adjusted(8.0, 6.0, -8.0, -6.0), Qt::AlignTop | Qt::AlignLeft, label);
  painter.restore();
}

void CompositionViewport::drawCamera(QPainter& painter, const render::RenderedCamera& camera, const QRectF& world) const {
  const timeline::Transform2D transform = camera.state.transform;
  QRectF cameraRect = worldRect(2.1, 1.18, transform, world);
  const bool isSelected = selected(camera.cameraNodeId);

  painter.save();
  painter.translate(cameraRect.center());
  painter.rotate(transform.rotationDegrees);
  cameraRect.moveCenter(QPointF{0.0, 0.0});

  painter.setPen(isSelected ? QPen{QColor{"#061019"}, 5} : QPen{QColor{"#6be7f5"}, 2});
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(cameraRect);

  painter.setBrush(QColor{"#0d1822"});
  painter.setPen(QPen{QColor{"#d8f3ff"}, 1});
  const QRectF icon{-13.0, -10.0, 26.0, 20.0};
  painter.drawRoundedRect(icon, 5.0, 5.0);
  painter.drawEllipse(QPointF{0.0, 0.0}, 4.0, 4.0);

  painter.setFont(QFont{"DejaVu Sans", 9, QFont::Bold});
  const QString label = QFontMetrics{painter.font()}.elidedText(
    qString(cameraLabel(camera)),
    Qt::ElideRight,
    static_cast<int>(std::max(20.0, cameraRect.width() - 16.0))
  );
  const QRectF labelRect = cameraRect.adjusted(8.0, cameraRect.height() - 24.0, -8.0, -6.0);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor{"#09131d"});
  painter.drawRoundedRect(labelRect, 4.0, 4.0);
  painter.setPen(QColor{"#d8f3ff"});
  painter.drawText(labelRect.adjusted(6.0, 0.0, -6.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, label);
  painter.restore();
}

QRectF CompositionViewport::worldRect(
  double width,
  double height,
  const foundation::Transform2D& transform,
  const QRectF& world
) const {
  const double pixelsPerWorldUnit = std::min(world.width(), world.height()) * 0.28;
  const double pixelWidth = width * positiveScale(transform.scale.x) * pixelsPerWorldUnit;
  const double pixelHeight = height * positiveScale(transform.scale.y) * pixelsPerWorldUnit;
  const QPointF center{
    world.center().x() + (transform.position.x * pixelsPerWorldUnit),
    world.center().y() - (transform.position.y * pixelsPerWorldUnit)
  };
  return QRectF{
    center.x() - (pixelWidth * 0.5),
    center.y() - (pixelHeight * 0.5),
    pixelWidth,
    pixelHeight
  };
}

std::optional<foundation::Resolution> CompositionViewport::dimensionsFor(const foundation::AssetId& assetId) const {
  if (!viewModel_.has_value()) {
    return std::nullopt;
  }
  for (const app::AppAssetRow& asset : viewModel_->assets.rows) {
    if (asset.assetId == assetId) {
      return asset.dimensions;
    }
  }
  return std::nullopt;
}

std::string CompositionViewport::mediaFrameLabel(const render::RenderedMediaFrame& mediaFrame) const {
  if (viewModel_.has_value()) {
    for (const app::AppClipRow& clip : viewModel_->timeline.clips) {
      if (clip.sourceNodeId == mediaFrame.clipNodeId) {
        return clip.assetName;
      }
    }
  }
  return mediaFrame.assetId.value();
}

std::string CompositionViewport::cameraLabel(const render::RenderedCamera& camera) const {
  if (viewModel_.has_value()) {
    for (const app::AppCameraRow& row : viewModel_->timeline.cameras) {
      if (row.sourceNodeId == camera.cameraNodeId) {
        return row.name;
      }
    }
  }
  return camera.cameraNodeId.value();
}

bool CompositionViewport::selected(const foundation::NodeId& nodeId) const {
  return selectedNodeId_.has_value() && selectedNodeId_.value() == nodeId;
}

} // namespace grapple::ui
