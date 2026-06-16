#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/ui_qt/AssetThumbnailCache.hpp>

#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

class QColor;
class QMouseEvent;
class QPaintEvent;
class QPainter;

namespace grapple::ui {

class TimelinePanel final : public QWidget {
public:
  explicit TimelinePanel(QWidget* parent = nullptr);

  void setPackageRoot(foundation::FilePath packageRoot);
  void setViewModel(app::AppViewModel viewModel);
  void setPlayhead(foundation::TimeSeconds playhead);
  void setSelectedNodeId(std::optional<foundation::NodeId> selectedNodeId);
  void setSeekHandler(std::function<void(foundation::TimeSeconds)> seekHandler);
  void setSelectionHandler(std::function<void(foundation::NodeId)> selectionHandler);
  [[nodiscard]] QString emptyPromptText() const;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  [[nodiscard]] int rulerHeight() const noexcept;
  [[nodiscard]] int rowHeight() const noexcept;
  [[nodiscard]] double duration() const noexcept;
  [[nodiscard]] int timelineLeft() const noexcept;
  [[nodiscard]] int timelineRight() const noexcept;
  [[nodiscard]] foundation::TimeSeconds timeAtX(int x) const noexcept;
  [[nodiscard]] std::optional<foundation::NodeId> nodeAt(const QPoint& point) const;
  [[nodiscard]] QRect clipRectFor(int row, const app::AppClipRow& clip) const;
  [[nodiscard]] QRect textClipRectFor(int row, const app::AppTextClipRow& clip) const;
  [[nodiscard]] QRect cameraRectFor(int row, std::size_t cameraIndex, std::size_t cameraCount) const;

  static int clipX(foundation::TimeSeconds time, int left, int trackWidth, double duration);
  static QString elidedText(QPainter& painter, const QString& text, int width);
  void drawLayerRow(
    QPainter& painter,
    const app::AppLayerRow& layer,
    const std::vector<app::AppClipRow>& clips,
    const std::vector<app::AppTextClipRow>& textClips,
    const QRect& row,
    int left,
    const QColor& rowColor,
    const QColor& borderColor,
    const QColor& clipColor
  ) const;
  void drawCameraRow(QPainter& painter, const app::AppViewModel& viewModel, const QRect& row, int left) const;
  void drawEmptyTimelinePrompt(QPainter& painter, const QRect& bounds) const;
  void drawPlayhead(QPainter& painter, int left, int trackWidth, double duration) const;

  std::optional<app::AppViewModel> viewModel_;
  AssetThumbnailCache thumbnailCache_;
  foundation::TimeSeconds playhead_;
  std::optional<foundation::NodeId> selectedNodeId_;
  std::function<void(foundation::TimeSeconds)> seekHandler_;
  std::function<void(foundation::NodeId)> selectionHandler_;
};

} // namespace grapple::ui
