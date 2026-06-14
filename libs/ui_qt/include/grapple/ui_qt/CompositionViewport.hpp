#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/render/RenderFrame.hpp>

#include <QWidget>

#include <optional>

class QPaintEvent;
class QPainter;

namespace grapple::ui {

class CompositionViewport final : public QWidget {
public:
  explicit CompositionViewport(QWidget* parent = nullptr);

  void setViewModel(app::AppViewModel viewModel);
  void setFrame(render::RenderFrame frame);
  void setPlayhead(foundation::TimeSeconds playhead);
  void setSelectedNodeId(std::optional<foundation::NodeId> selectedNodeId);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  void drawGrid(QPainter& painter, const QRectF& world) const;
  void drawClip(QPainter& painter, const app::AppClipRow& clip, const QRectF& world) const;
  void drawCamera(QPainter& painter, const app::AppCameraRow& camera, const QRectF& world) const;
  [[nodiscard]] timeline::Transform2D evaluatedCameraTransform(const app::AppCameraRow& camera) const;
  [[nodiscard]] QRectF worldRect(
    double width,
    double height,
    const foundation::Transform2D& transform,
    const QRectF& world
  ) const;
  [[nodiscard]] std::optional<foundation::Resolution> dimensionsFor(const foundation::AssetId& assetId) const;
  [[nodiscard]] bool selected(const foundation::NodeId& nodeId) const;
  [[nodiscard]] bool activeAtPlayhead(const app::AppClipRow& clip) const;

  std::optional<app::AppViewModel> viewModel_;
  std::optional<render::RenderFrame> frame_;
  foundation::TimeSeconds playhead_;
  std::optional<foundation::NodeId> selectedNodeId_;
};

} // namespace grapple::ui
