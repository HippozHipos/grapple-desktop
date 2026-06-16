#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/render/RenderFrame.hpp>
#include <grapple/ui_qt/AssetThumbnailCache.hpp>

#include <QWidget>

#include <memory>
#include <optional>

class QPaintEvent;
class QPainter;

namespace grapple::ui {

class CompositionViewport final : public QWidget {
public:
  explicit CompositionViewport(QWidget* parent = nullptr);

  void setPackageRoot(foundation::FilePath packageRoot);
  void setViewModel(app::AppViewModel viewModel);
  void setFrame(std::shared_ptr<const render::RenderFrame> frame);
  void setPlayhead(foundation::TimeSeconds playhead);
  void setSelectedNodeId(std::optional<foundation::NodeId> selectedNodeId);

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  void drawGrid(QPainter& painter, const QRectF& world) const;
  void drawMediaFrame(QPainter& painter, const render::RenderedMediaFrame& mediaFrame, const QRectF& world) const;
  void drawTextFrame(QPainter& painter, const render::RenderedTextFrame& textFrame, const QRectF& world) const;
  void drawCamera(QPainter& painter, const render::RenderedCamera& camera, const QRectF& world) const;
  [[nodiscard]] QRectF worldRect(
    double width,
    double height,
    const foundation::Transform2D& transform,
    const QRectF& world
  ) const;
  [[nodiscard]] std::optional<foundation::Resolution> dimensionsFor(const foundation::AssetId& assetId) const;
  [[nodiscard]] std::string mediaFrameLabel(const render::RenderedMediaFrame& mediaFrame) const;
  [[nodiscard]] std::string cameraLabel(const render::RenderedCamera& camera) const;
  [[nodiscard]] bool selected(const foundation::NodeId& nodeId) const;
  void updateFrameToolTip();

  std::optional<app::AppViewModel> viewModel_;
  std::shared_ptr<const render::RenderFrame> frame_;
  AssetThumbnailCache thumbnailCache_;
  foundation::TimeSeconds playhead_;
  std::optional<foundation::NodeId> selectedNodeId_;
};

} // namespace grapple::ui
