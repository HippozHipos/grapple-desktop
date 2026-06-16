#include <grapple/ui_qt/TimelinePanel.hpp>

#include <QColor>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>

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
  thumbnailCache_.setAssets(viewModel_->assets);
  update();
}

void TimelinePanel::setPackageRoot(foundation::FilePath packageRoot) {
  thumbnailCache_.setPackageRoot(std::move(packageRoot));
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

QString TimelinePanel::emptyPromptText() const {
  return "Timeline is empty\n1. Use Sample to start now\n2. Or import/drop media, then double-click it\n3. Ask Steward for an editable change";
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

  if (viewModel.timeline.layers.empty() &&
      viewModel.timeline.audioTracks.empty() &&
      viewModel.timeline.cameras.empty()) {
    drawEmptyTimelinePrompt(
      painter,
      QRect{left, rulerHeight, trackWidth, std::max(1, height() - rulerHeight)}
    );
    drawPlayhead(painter, left, trackWidth, duration);
    return;
  }

  int y = rulerHeight;
  for (const app::AppLayerRow& layer : viewModel.timeline.layers) {
    drawLayerRow(
      painter,
      layer,
      viewModel.timeline.clips,
      viewModel.timeline.textClips,
      QRect{0, y, width(), rowHeight},
      left,
      QColor{"#242936"},
      QColor{"#3b4556"},
      QColor{"#36466e"}
    );
    y += rowHeight;
  }

  for (const app::AppLayerRow& track : viewModel.timeline.audioTracks) {
    drawLayerRow(
      painter,
      track,
      viewModel.timeline.audioClips,
      {},
      QRect{0, y, width(), rowHeight},
      left,
      QColor{"#242d2b"},
      QColor{"#3c544d"},
      QColor{"#2f5d54"}
    );
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

  if (point.y() < rulerHeight()) {
    return std::nullopt;
  }
  const int row = (point.y() - rulerHeight()) / rowHeight();

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
    for (const app::AppTextClipRow& clip : viewModel_->timeline.textClips) {
      if (clip.trackNodeId != layer.sourceNodeId) {
        continue;
      }
      if (textClipRectFor(row, clip).contains(point)) {
        return clip.sourceNodeId;
      }
    }
    return layer.sourceNodeId;
  }

  const int firstAudioRow = static_cast<int>(viewModel_->timeline.layers.size());
  const int firstCameraRow = firstAudioRow + static_cast<int>(viewModel_->timeline.audioTracks.size());
  if (row >= firstAudioRow && row < firstCameraRow) {
    const app::AppLayerRow& track = viewModel_->timeline.audioTracks[static_cast<std::size_t>(row - firstAudioRow)];
    for (const app::AppClipRow& clip : viewModel_->timeline.audioClips) {
      if (clip.trackNodeId != track.sourceNodeId) {
        continue;
      }
      if (clipRectFor(row, clip).contains(point)) {
        return clip.sourceNodeId;
      }
    }
    return track.sourceNodeId;
  }

  if (row == firstCameraRow && !viewModel_->timeline.cameras.empty()) {
    for (std::size_t index = 0; index < viewModel_->timeline.cameras.size(); ++index) {
      if (cameraRectFor(row, index, viewModel_->timeline.cameras.size()).contains(point)) {
        return viewModel_->timeline.cameras[index].sourceNodeId;
      }
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

QRect TimelinePanel::textClipRectFor(int row, const app::AppTextClipRow& clip) const {
  const int left = timelineLeft();
  const int width = std::max(1, timelineRight() - left);
  const int top = rulerHeight() + (row * rowHeight());
  const int x = clipX(clip.timelineRange.start, left, width, duration());
  const int endX = clipX(clip.timelineRange.end, left, width, duration());
  return QRect{x + 2, top + 8, std::max(18, endX - x - 4), rowHeight() - 16};
}

QRect TimelinePanel::cameraRectFor(int row, std::size_t cameraIndex, std::size_t cameraCount) const {
  const int left = timelineLeft();
  const int width = std::max(1, timelineRight() - left);
  const int top = rulerHeight() + (row * rowHeight());
  const int gap = 4;
  const int laneTop = top + 6;
  const int availableHeight = rowHeight() - 12;
  const int count = std::max(1, static_cast<int>(cameraCount));
  const int laneHeight = std::max(12, (availableHeight - (gap * (count - 1))) / count);
  const int y = laneTop + (static_cast<int>(cameraIndex) * (laneHeight + gap));
  return QRect{left + 2, y, std::max(18, width - 4), laneHeight};
}

int TimelinePanel::clipX(foundation::TimeSeconds time, int left, int trackWidth, double duration) {
  return left + static_cast<int>((time.value / duration) * static_cast<double>(trackWidth));
}

QString TimelinePanel::elidedText(QPainter& painter, const QString& text, int width) {
  return QFontMetrics{painter.font()}.elidedText(text, Qt::ElideRight, std::max(12, width));
}

void TimelinePanel::drawLayerRow(
  QPainter& painter,
  const app::AppLayerRow& layer,
  const std::vector<app::AppClipRow>& clips,
  const std::vector<app::AppTextClipRow>& textClips,
  const QRect& row,
  int left,
  const QColor& rowColor,
  const QColor& borderColor,
  const QColor& clipColor
) const {
  painter.fillRect(row, rowColor);
  painter.setPen(borderColor);
  painter.drawLine(0, row.bottom(), width(), row.bottom());
  painter.setPen(QColor{"#dbe7f7"});
  painter.drawText(QRect{16, row.top(), left - 28, row.height()}, Qt::AlignVCenter | Qt::AlignLeft, qString(layer.name));

  for (const app::AppClipRow& clip : clips) {
    if (clip.trackNodeId != layer.sourceNodeId) {
      continue;
    }
    const QRect clipRect = clipRectFor((row.top() - rulerHeight()) / rowHeight(), clip);
    const bool selected = selectedNodeId_.has_value() && clip.sourceNodeId == selectedNodeId_.value();
    painter.setPen(selected ? QPen{QColor{"#ffffff"}, 3} : QPen{QColor{"#b9c7f0"}, 1});
    painter.setBrush(clipColor);
    painter.drawRoundedRect(clipRect, 6, 6);
    int textLeftInset = 10;
    if (const QPixmap* thumbnail = thumbnailCache_.thumbnailFor(clip.assetId);
        thumbnail != nullptr && !thumbnail->isNull() && clipRect.width() > 72) {
      const int thumbnailWidth = std::min(52, std::max(24, clipRect.width() / 4));
      const QRect thumbnailRect{
        clipRect.left() + 4,
        clipRect.top() + 4,
        thumbnailWidth,
        clipRect.height() - 8
      };
      painter.drawPixmap(thumbnailRect, *thumbnail, thumbnail->rect());
      textLeftInset = thumbnailWidth + 12;
    }
    painter.setPen(QColor{"#eef4ff"});
    painter.drawText(
      clipRect.adjusted(textLeftInset, 0, -8, 0),
      Qt::AlignVCenter | Qt::AlignLeft,
      elidedText(painter, qString(clip.assetName), clipRect.width() - textLeftInset - 8)
    );
  }

  for (const app::AppTextClipRow& clip : textClips) {
    if (clip.trackNodeId != layer.sourceNodeId) {
      continue;
    }
    const QRect clipRect = textClipRectFor((row.top() - rulerHeight()) / rowHeight(), clip);
    const bool selected = selectedNodeId_.has_value() && clip.sourceNodeId == selectedNodeId_.value();
    painter.setPen(selected ? QPen{QColor{"#ffffff"}, 3} : QPen{QColor{"#f6d56f"}, 1});
    painter.setBrush(QColor{"#5a4721"});
    painter.drawRoundedRect(clipRect, 6, 6);
    painter.setPen(QColor{"#fff7cf"});
    painter.drawText(
      clipRect.adjusted(10, 0, -8, 0),
      Qt::AlignVCenter | Qt::AlignLeft,
      elidedText(painter, qString(clip.text), clipRect.width() - 18)
    );
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

  const int cameraRowIndex = (row.top() - rulerHeight()) / rowHeight();
  for (std::size_t index = 0; index < viewModel.timeline.cameras.size(); ++index) {
    const app::AppCameraRow& camera = viewModel.timeline.cameras[index];
    const QRect cameraStrip = cameraRectFor(cameraRowIndex, index, viewModel.timeline.cameras.size());
    const bool selected = selectedNodeId_.has_value() && camera.sourceNodeId == selectedNodeId_.value();
    painter.setPen(selected ? QPen{QColor{"#ffffff"}, 3} : QPen{QColor{"#86e8f2"}, 1});
    painter.setBrush(QColor{"#23535e"});
    painter.drawRoundedRect(cameraStrip, 6, 6);
    painter.setPen(QColor{"#e5fdff"});
    painter.drawText(
      cameraStrip.adjusted(10, 0, -8, 0),
      Qt::AlignVCenter | Qt::AlignLeft,
      elidedText(painter, qString(camera.name), cameraStrip.width() - 18)
    );
  }
}

void TimelinePanel::drawEmptyTimelinePrompt(QPainter& painter, const QRect& bounds) const {
  painter.setPen(QColor{"#9fb0c8"});
  painter.setFont(QFont{"DejaVu Sans", 12, QFont::Bold});
  painter.drawText(
    bounds.adjusted(18, 18, -18, -18),
    Qt::AlignCenter,
    emptyPromptText()
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
