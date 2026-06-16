#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/render/RenderFrame.hpp>

#include <QPixmap>
#include <QWidget>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

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
  void drawCamera(QPainter& painter, const render::RenderedCamera& camera, const QRectF& world) const;
  [[nodiscard]] QRectF worldRect(
    double width,
    double height,
    const foundation::Transform2D& transform,
    const QRectF& world
  ) const;
  void rebuildThumbnailImages();
  [[nodiscard]] QString resolvedAssetPath(const foundation::FilePath& path) const;
  [[nodiscard]] const QPixmap* thumbnailFor(const foundation::AssetId& assetId) const;
  [[nodiscard]] std::optional<foundation::Resolution> dimensionsFor(const foundation::AssetId& assetId) const;
  [[nodiscard]] std::string mediaFrameLabel(const render::RenderedMediaFrame& mediaFrame) const;
  [[nodiscard]] std::string cameraLabel(const render::RenderedCamera& camera) const;
  [[nodiscard]] bool selected(const foundation::NodeId& nodeId) const;

  std::optional<app::AppViewModel> viewModel_;
  std::shared_ptr<const render::RenderFrame> frame_;
  foundation::FilePath packageRoot_;
  std::vector<std::pair<foundation::AssetId, QPixmap>> thumbnailImages_;
  foundation::TimeSeconds playhead_;
  std::optional<foundation::NodeId> selectedNodeId_;
};

} // namespace grapple::ui
