#include <grapple/ui_qt/TimelinePanel.hpp>

#include <QColor>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

} // namespace

TimelinePanel::TimelinePanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("timeline");
  setMinimumHeight(210);
}

void TimelinePanel::setViewModel(app::AppViewModel viewModel) {
  viewModel_ = std::move(viewModel);
  update();
}

void TimelinePanel::setPlayhead(foundation::TimeSeconds playhead) {
  playhead_ = playhead;
  update();
}

void TimelinePanel::setSelectedNodeId(std::optional<foundation::NodeId> selectedNodeId) {
  selectedNodeId_ = std::move(selectedNodeId);
  update();
}

void TimelinePanel::setSeekHandler(std::function<void(foundation::TimeSeconds)> seekHandler) {
  seekHandler_ = std::move(seekHandler);
}

void TimelinePanel::setSelectionHandler(std::function<void(foundation::NodeId)> selectionHandler) {
  selectionHandler_ = std::move(selectionHandler);
}

void TimelinePanel::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);

  QPainter painter{this};
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), QColor{"#20242d"});

  if (!viewModel_.has_value()) {
    painter.setPen(QColor{"#9aa8bd"});
    painter.drawText(rect().adjusted(18, 18, -18, -18), Qt::AlignLeft | Qt::AlignTop, "No timeline loaded");
    return;
  }

  const app::AppViewModel& viewModel = viewModel_.value();
  const int labelWidth = 150;
  const int rulerHeight = 34;
  const int rowHeight = 44;
  const int left = labelWidth;
  const int right = width() - 16;
  const int trackWidth = std::max(1, right - left);
  const double duration = std::max(0.001, viewModel.timeline.duration.value);

  painter.setPen(QColor{"#58647a"});
  painter.drawLine(left, rulerHeight - 1, right, rulerHeight - 1);
  painter.setFont(QFont{"DejaVu Sans", 9});

  const int tickCount = std::max(1, static_cast<int>(duration));
  for (int tick = 0; tick <= tickCount; ++tick) {
    const double time = duration * static_cast<double>(tick) / static_cast<double>(tickCount);
    const int x = left + static_cast<int>((time / duration) * trackWidth);
    painter.setPen(QColor{tick == 0 ? "#7b8aa3" : "#3d4656"});
    painter.drawLine(x, 0, x, height());
    painter.setPen(QColor{"#aab8cf"});
    const QString label = QString{"%1s"}.arg(time, 0, 'f', 1);
    const int labelX = std::min(x + 4, right - QFontMetrics{painter.font()}.horizontalAdvance(label) - 2);
    painter.drawText(labelX, 22, label);
  }

  int y = rulerHeight;
  for (const app::AppLayerRow& layer : viewModel.timeline.layers) {
    drawLayerRow(painter, viewModel, layer, QRect{0, y, width(), rowHeight}, left);
    y += rowHeight;
  }

  if (!viewModel.timeline.cameras.empty()) {
    drawCameraRow(painter, viewModel, QRect{0, y, width(), rowHeight}, left);
  }

  drawPlayhead(painter, left, trackWidth, duration);
}

void TimelinePanel::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !viewModel_.has_value()) {
    QWidget::mousePressEvent(event);
    return;
  }

  if (const auto selected = nodeAt(event->pos()); selected.has_value() && selectionHandler_) {
    selectionHandler_(selected.value());
    return;
  }

  if (seekHandler_) {
    seekHandler_(timeAtX(event->pos().x()));
  }
}

int TimelinePanel::rulerHeight() const noexcept {
  return 34;
}

int TimelinePanel::rowHeight() const noexcept {
  return 44;
}

double TimelinePanel::duration() const noexcept {
  return viewModel_.has_value()
    ? std::max(0.001, viewModel_->timeline.duration.value)
    : 0.001;
}

int TimelinePanel::timelineLeft() const noexcept {
  return 150;
}

int TimelinePanel::timelineRight() const noexcept {
  return width() - 16;
}

foundation::TimeSeconds TimelinePanel::timeAtX(int x) const noexcept {
  const int left = timelineLeft();
  const int right = timelineRight();
  const double normalized = std::clamp(
    static_cast<double>(x - left) / static_cast<double>(std::max(1, right - left)),
    0.0,
    1.0
  );
  return foundation::TimeSeconds{normalized * duration()};
}

std::optional<foundation::NodeId> TimelinePanel::nodeAt(const QPoint& point) const {
  if (!viewModel_.has_value()) {
    return std::nullopt;
  }

  const int row = (point.y() - rulerHeight()) / rowHeight();
  if (row < 0) {
    return std::nullopt;
  }

  if (row < static_cast<int>(viewModel_->timeline.layers.size())) {
    const app::AppLayerRow& layer = viewModel_->timeline.layers[static_cast<std::size_t>(row)];
    for (const app::AppClipRow& clip : viewModel_->timeline.clips) {
      if (clip.trackNodeId != layer.sourceNodeId) {
        continue;
      }
      if (clipRectFor(row, clip).contains(point)) {
        return clip.sourceNodeId;
      }
    }
    return std::nullopt;
  }

  if (row == static_cast<int>(viewModel_->timeline.layers.size()) && !viewModel_->timeline.cameras.empty()) {
    if (cameraRectFor(row).contains(point)) {
      return viewModel_->timeline.cameras.front().sourceNodeId;
    }
  }

  return std::nullopt;
}

QRect TimelinePanel::clipRectFor(int row, const app::AppClipRow& clip) const {
  const int left = timelineLeft();
  const int width = std::max(1, timelineRight() - left);
  const int top = rulerHeight() + (row * rowHeight());
  const int x = clipX(clip.timelineRange.start, left, width, duration());
  const int endX = clipX(clip.timelineRange.end, left, width, duration());
  return QRect{x + 2, top + 8, std::max(18, endX - x - 4), rowHeight() - 16};
}

QRect TimelinePanel::cameraRectFor(int row) const {
  const int left = timelineLeft();
  const int width = std::max(1, timelineRight() - left);
  const int top = rulerHeight() + (row * rowHeight());
  return QRect{left + 2, top + 10, std::max(18, width - 4), rowHeight() - 20};
}

int TimelinePanel::clipX(foundation::TimeSeconds time, int left, int trackWidth, double duration) {
  return left + static_cast<int>((time.value / duration) * static_cast<double>(trackWidth));
}

QString TimelinePanel::elidedText(QPainter& painter, const QString& text, int width) {
  return QFontMetrics{painter.font()}.elidedText(text, Qt::ElideRight, std::max(12, width));
}

void TimelinePanel::drawLayerRow(
  QPainter& painter,
  const app::AppViewModel& viewModel,
  const app::AppLayerRow& layer,
  const QRect& row,
  int left
) const {
  painter.fillRect(row, QColor{"#242936"});
  painter.setPen(QColor{"#3b4556"});
  painter.drawLine(0, row.bottom(), width(), row.bottom());
  painter.setPen(QColor{"#dbe7f7"});
  painter.drawText(QRect{16, row.top(), left - 28, row.height()}, Qt::AlignVCenter | Qt::AlignLeft, qString(layer.name));

  for (const app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.trackNodeId != layer.sourceNodeId) {
      continue;
    }
    const QRect clipRect = clipRectFor((row.top() - rulerHeight()) / rowHeight(), clip);
    const bool selected = selectedNodeId_.has_value() && clip.sourceNodeId == selectedNodeId_.value();
    painter.setPen(selected ? QPen{QColor{"#ffffff"}, 3} : QPen{QColor{"#b9c7f0"}, 1});
    painter.setBrush(QColor{"#36466e"});
    painter.drawRoundedRect(clipRect, 6, 6);
    painter.setPen(QColor{"#eef4ff"});
    painter.drawText(clipRect.adjusted(10, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, elidedText(painter, qString(clip.assetId.value()), clipRect.width() - 18));
  }
}

void TimelinePanel::drawCameraRow(
  QPainter& painter,
  const app::AppViewModel& viewModel,
  const QRect& row,
  int left
) const {
  painter.fillRect(row, QColor{"#1f2c33"});
  painter.setPen(QColor{"#3b5964"});
  painter.drawLine(0, row.bottom(), width(), row.bottom());
  painter.setPen(QColor{"#d7f8ff"});
  painter.drawText(QRect{16, row.top(), left - 28, row.height()}, Qt::AlignVCenter | Qt::AlignLeft, "Cameras");

  const QRect cameraStrip = cameraRectFor((row.top() - rulerHeight()) / rowHeight());
  const bool selected = selectedNodeId_.has_value() &&
                        !viewModel.timeline.cameras.empty() &&
                        viewModel.timeline.cameras.front().sourceNodeId == selectedNodeId_.value();
  painter.setPen(selected ? QPen{QColor{"#ffffff"}, 3} : QPen{QColor{"#86e8f2"}, 1});
  painter.setBrush(QColor{"#23535e"});
  painter.drawRoundedRect(cameraStrip, 6, 6);
  painter.setPen(QColor{"#e5fdff"});
  painter.drawText(
    cameraStrip.adjusted(10, 0, -8, 0),
    Qt::AlignVCenter | Qt::AlignLeft,
    elidedText(painter, qString(viewModel.timeline.cameras.front().name), cameraStrip.width() - 18)
  );
}

void TimelinePanel::drawPlayhead(QPainter& painter, int left, int trackWidth, double duration) const {
  const double normalized = std::clamp(playhead_.value / duration, 0.0, 1.0);
  const int x = left + static_cast<int>(normalized * static_cast<double>(trackWidth));
  painter.setPen(QPen{QColor{"#ff5b5b"}, 2});
  painter.drawLine(x, 0, x, height());
  painter.setBrush(QColor{"#ff5b5b"});
  painter.drawPolygon(QPolygon{{QPoint{x - 6, 0}, QPoint{x + 6, 0}, QPoint{x, 9}}});
}

} // namespace grapple::ui
