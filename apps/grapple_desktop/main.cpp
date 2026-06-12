#include <DemoProject.hpp>

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/media/MediaSource.hpp>

#include <QApplication>
#include <QColor>
#include <QFrame>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

QString timeText(grapple::foundation::TimeSeconds time) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << time.value << "s";
  return qString(output.str());
}

QString summaryText(const grapple::app::AppViewModel& viewModel) {
  return QString{
    "Project: %1\nRevision: %2\nDuration: %3s\nAssets: %4\nCompositions: %5\nLayers: %6\nClips: %7\nCameras: %8\nEffect graphs: %9"
  }
    .arg(qString(viewModel.project.projectId.value()))
    .arg(qString(viewModel.project.revision.value()))
    .arg(viewModel.timeline.duration.value)
    .arg(viewModel.assets.count)
    .arg(viewModel.timeline.compositions.size())
    .arg(viewModel.timeline.layers.size())
    .arg(viewModel.timeline.clips.size())
    .arg(viewModel.timeline.cameras.size())
    .arg(viewModel.timeline.effectGraphs.size());
}

QString mediaBinText(const grapple::app::AppViewModel& viewModel) {
  QStringList lines;
  lines << "Media Bin";
  if (viewModel.assets.rows.empty()) {
    lines << "No assets";
    return lines.join('\n');
  }

  for (const grapple::app::AppAssetRow& asset : viewModel.assets.rows) {
    QString details = QString{"%1  [%2]\n%3"}
      .arg(qString(asset.name))
      .arg(qString(asset.mediaType))
      .arg(qString(asset.assetId.value()));

    QStringList metadata;
    if (asset.duration.has_value()) {
      metadata << QString{"%1s"}.arg(asset.duration->value, 0, 'f', 2);
    }
    if (asset.dimensions.has_value()) {
      metadata << QString{"%1x%2"}.arg(asset.dimensions->width).arg(asset.dimensions->height);
    }
    if (!metadata.empty()) {
      details += "\n" + metadata.join("  ");
    }
    details += "\n" + qString(asset.sourcePath.value);
    lines << "" << details;
  }

  return lines.join('\n');
}

QString inspectorText(
  const grapple::app::AppViewModel& viewModel,
  const std::optional<grapple::foundation::NodeId>& selectedNodeId
) {
  if (!selectedNodeId.has_value()) {
    return "Inspector\nNo selection";
  }

  for (const grapple::app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nClip %1\nAsset: %2\nTrack: %3\nRange: %4s - %5s"}
        .arg(qString(clip.sourceNodeId.value()))
        .arg(qString(clip.assetId.value()))
        .arg(qString(clip.trackNodeId.value()))
        .arg(clip.timelineRange.start.value)
        .arg(clip.timelineRange.end.value);
    }
  }

  for (const grapple::app::AppCameraRow& camera : viewModel.timeline.cameras) {
    if (camera.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nCamera %1\nName: %2"}
        .arg(qString(camera.sourceNodeId.value()))
        .arg(qString(camera.name));
    }
  }

  for (const grapple::app::AppLayerRow& layer : viewModel.timeline.layers) {
    if (layer.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nLayer %1\nClips: %2"}
        .arg(qString(layer.name))
        .arg(layer.clipCount);
    }
  }

  return QString{"Inspector\nUnknown node %1"}.arg(qString(selectedNodeId->value()));
}

QString mediaKindText(grapple::render::RenderedMediaKind kind) {
  switch (kind) {
    case grapple::render::RenderedMediaKind::Video:
      return "Video";
    case grapple::render::RenderedMediaKind::Image:
      return "Image";
  }

  return "Unknown";
}

grapple::foundation::Result<void> ensureDemoVideoFile(const grapple::foundation::FilePath& path) {
  constexpr int width = 320;
  constexpr int height = 180;
  constexpr int frameCount = 300;
  const std::filesystem::path videoPath{path.value};
  std::filesystem::create_directories(videoPath.parent_path());

  cv::VideoWriter writer{
    path.value,
    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
    30.0,
    cv::Size{width, height}
  };
  if (!writer.isOpened()) {
    return grapple::foundation::Error{"desktop.demo_video_open_failed", "Could not create demo video " + path.value + "."};
  }

  for (int frame = 0; frame < frameCount; ++frame) {
    cv::Mat image(height, width, CV_8UC3);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        image.at<cv::Vec3b>(y, x) = cv::Vec3b{
          static_cast<unsigned char>((180 + frame * 2) % 255),
          static_cast<unsigned char>((y * 2 + 80) % 255),
          static_cast<unsigned char>((x + frame * 4) % 255)
        };
      }
    }
    writer.write(image);
  }

  return {};
}

class PreviewSurface final : public QWidget {
public:
  explicit PreviewSurface(QWidget* parent = nullptr)
    : QWidget{parent} {
    setObjectName("previewSurface");
    setMinimumSize(480, 280);
  }

  void setFrame(grapple::render::RenderFrame frame) {
    frame_ = std::move(frame);
    update();
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    QWidget::paintEvent(event);

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor{"#0e1118"});

    if (!frame_.has_value()) {
      drawCenteredText(painter, "No frame rendered");
      return;
    }

    const grapple::render::RenderFrame& frame = frame_.value();
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
    for (const grapple::render::RenderedMediaFrame& mediaFrame : frame.mediaFrames) {
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

private:
  static QString elidedText(QPainter& painter, const QString& text, int width) {
    return QFontMetrics{painter.font()}.elidedText(text, Qt::ElideRight, std::max(12, width));
  }

  void drawCenteredText(QPainter& painter, const QString& text) const {
    painter.setPen(QColor{"#d8f3ff"});
    painter.setFont(QFont{"DejaVu Sans", 16, QFont::Bold});
    painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter, text);
  }

  void drawRenderedImage(QPainter& painter, const grapple::render::RenderFrame& frame) const {
    const grapple::render::RenderedImage& image = frame.image.value();
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
      const grapple::render::RenderedMediaFrame& mediaFrame = frame.mediaFrames.front();
      painter.drawText(
        rect().adjusted(18, -32, -18, -10),
        Qt::AlignBottom | Qt::AlignHCenter,
        QString{"%1  %2"}.arg(qString(mediaFrame.assetId.value())).arg(timeText(mediaFrame.sourceTime))
      );
    }
  }

  static QRect scaledRect(const QSize& sourceSize, const QRect& bounds) {
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

  void drawMediaFrame(
    QPainter& painter,
    const grapple::render::RenderedMediaFrame& mediaFrame,
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
      QString{"asset %1"}.arg(qString(mediaFrame.assetId.value())),
      QString{"clip %1"}.arg(qString(mediaFrame.clipNodeId.value())),
      QString{"source %1"}.arg(timeText(mediaFrame.sourceTime))
    };
    int y = detailsRect.top();
    for (const QString& line : lines) {
      painter.drawText(detailsRect.left(), y + 18, elidedText(painter, line, detailsRect.width()));
      y += 24;
    }
  }

  std::optional<grapple::render::RenderFrame> frame_;
};

class TimelinePanel final : public QWidget {
public:
  explicit TimelinePanel(QWidget* parent = nullptr)
    : QWidget{parent} {
    setObjectName("timeline");
    setMinimumHeight(210);
  }

  void setViewModel(grapple::app::AppViewModel viewModel) {
    viewModel_ = std::move(viewModel);
    update();
  }

  void setPlayhead(grapple::foundation::TimeSeconds playhead) {
    playhead_ = playhead;
    update();
  }

  void setSelectedNodeId(std::optional<grapple::foundation::NodeId> selectedNodeId) {
    selectedNodeId_ = std::move(selectedNodeId);
    update();
  }

  void setSeekHandler(std::function<void(grapple::foundation::TimeSeconds)> seekHandler) {
    seekHandler_ = std::move(seekHandler);
  }

  void setSelectionHandler(std::function<void(grapple::foundation::NodeId)> selectionHandler) {
    selectionHandler_ = std::move(selectionHandler);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    QWidget::paintEvent(event);

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor{"#20242d"});

    if (!viewModel_.has_value()) {
      painter.setPen(QColor{"#9aa8bd"});
      painter.drawText(rect().adjusted(18, 18, -18, -18), Qt::AlignLeft | Qt::AlignTop, "No timeline loaded");
      return;
    }

    const grapple::app::AppViewModel& viewModel = viewModel_.value();
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
    for (const grapple::app::AppLayerRow& layer : viewModel.timeline.layers) {
      drawLayerRow(painter, viewModel, layer, QRect{0, y, width(), rowHeight}, left);
      y += rowHeight;
    }

    if (!viewModel.timeline.cameras.empty()) {
      drawCameraRow(painter, viewModel, QRect{0, y, width(), rowHeight}, left);
    }

    drawPlayhead(painter, left, trackWidth, duration);
  }

  void mousePressEvent(QMouseEvent* event) override {
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

private:
  [[nodiscard]] int rulerHeight() const noexcept {
    return 34;
  }

  [[nodiscard]] int rowHeight() const noexcept {
    return 44;
  }

  [[nodiscard]] double duration() const noexcept {
    return viewModel_.has_value()
      ? std::max(0.001, viewModel_->timeline.duration.value)
      : 0.001;
  }

  [[nodiscard]] int timelineLeft() const noexcept {
    return 150;
  }

  [[nodiscard]] int timelineRight() const noexcept {
    return width() - 16;
  }

  [[nodiscard]] grapple::foundation::TimeSeconds timeAtX(int x) const noexcept {
    const int left = timelineLeft();
    const int right = timelineRight();
    const double normalized = std::clamp(
      static_cast<double>(x - left) / static_cast<double>(std::max(1, right - left)),
      0.0,
      1.0
    );
    return grapple::foundation::TimeSeconds{normalized * duration()};
  }

  [[nodiscard]] std::optional<grapple::foundation::NodeId> nodeAt(const QPoint& point) const {
    if (!viewModel_.has_value()) {
      return std::nullopt;
    }

    const int row = (point.y() - rulerHeight()) / rowHeight();
    if (row < 0) {
      return std::nullopt;
    }

    if (row < static_cast<int>(viewModel_->timeline.layers.size())) {
      const grapple::app::AppLayerRow& layer = viewModel_->timeline.layers[static_cast<std::size_t>(row)];
      for (const grapple::app::AppClipRow& clip : viewModel_->timeline.clips) {
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

  [[nodiscard]] QRect clipRectFor(int row, const grapple::app::AppClipRow& clip) const {
    const int left = timelineLeft();
    const int width = std::max(1, timelineRight() - left);
    const int top = rulerHeight() + (row * rowHeight());
    const int x = clipX(clip.timelineRange.start, left, width, duration());
    const int endX = clipX(clip.timelineRange.end, left, width, duration());
    return QRect{x + 2, top + 8, std::max(18, endX - x - 4), rowHeight() - 16};
  }

  [[nodiscard]] QRect cameraRectFor(int row) const {
    const int left = timelineLeft();
    const int width = std::max(1, timelineRight() - left);
    const int top = rulerHeight() + (row * rowHeight());
    return QRect{left + 2, top + 10, std::max(18, width - 4), rowHeight() - 20};
  }

  static int clipX(const grapple::foundation::TimeSeconds time, int left, int trackWidth, double duration) {
    return left + static_cast<int>((time.value / duration) * static_cast<double>(trackWidth));
  }

  static QString elidedText(QPainter& painter, const QString& text, int width) {
    return QFontMetrics{painter.font()}.elidedText(text, Qt::ElideRight, std::max(12, width));
  }

  void drawLayerRow(
    QPainter& painter,
    const grapple::app::AppViewModel& viewModel,
    const grapple::app::AppLayerRow& layer,
    const QRect& row,
    int left
  ) const {
    painter.fillRect(row, QColor{"#242936"});
    painter.setPen(QColor{"#3b4556"});
    painter.drawLine(0, row.bottom(), width(), row.bottom());
    painter.setPen(QColor{"#dbe7f7"});
    painter.drawText(QRect{16, row.top(), left - 28, row.height()}, Qt::AlignVCenter | Qt::AlignLeft, qString(layer.name));

    for (const grapple::app::AppClipRow& clip : viewModel.timeline.clips) {
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

  void drawCameraRow(
    QPainter& painter,
    const grapple::app::AppViewModel& viewModel,
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

  void drawPlayhead(QPainter& painter, int left, int trackWidth, double duration) const {
    const double normalized = std::clamp(playhead_.value / duration, 0.0, 1.0);
    const int x = left + static_cast<int>(normalized * static_cast<double>(trackWidth));
    painter.setPen(QPen{QColor{"#ff5b5b"}, 2});
    painter.drawLine(x, 0, x, height());
    painter.setBrush(QColor{"#ff5b5b"});
    painter.drawPolygon(QPolygon{{QPoint{x - 6, 0}, QPoint{x + 6, 0}, QPoint{x, 9}}});
  }

  std::optional<grapple::app::AppViewModel> viewModel_;
  grapple::foundation::TimeSeconds playhead_;
  std::optional<grapple::foundation::NodeId> selectedNodeId_;
  std::function<void(grapple::foundation::TimeSeconds)> seekHandler_;
  std::function<void(grapple::foundation::NodeId)> selectionHandler_;
};

grapple::foundation::Result<void> populateDemo(grapple::app::NativeProjectSession& session, bool savePackage) {
  return grapple::demo::populateWalkingWomanDemo(
    session,
    savePackage
      ? std::optional<grapple::storage::SnapshotCommitRecord>{grapple::storage::SnapshotCommitRecord{
          grapple::foundation::SnapshotId{"snap_desktop_rev_6"},
          grapple::foundation::FilePath{"snapshots/rev_6.json"},
          std::optional<std::string>{"desktop"}
        }}
      : std::nullopt
  );
}

grapple::project::CommandSource userSource() {
  return grapple::project::CommandSource{
    grapple::project::CommandSourceKind::User,
    std::nullopt,
    "desktop"
  };
}

grapple::project::CommandSource importerSource() {
  return grapple::project::CommandSource{
    grapple::project::CommandSourceKind::Importer,
    std::nullopt,
    "desktop"
  };
}

grapple::foundation::Result<grapple::asset::Asset> inspectVideoAsset(
  const grapple::foundation::AssetId& assetId,
  const grapple::foundation::FilePath& path
) {
  cv::VideoCapture capture{path.value};
  if (!capture.isOpened()) {
    return grapple::foundation::Error{"desktop.video_open_failed", "Could not inspect video file " + path.value + "."};
  }

  const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
  const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  const double frameCount = capture.get(cv::CAP_PROP_FRAME_COUNT);
  const double framesPerSecond = capture.get(cv::CAP_PROP_FPS);
  if (width <= 0 || height <= 0 || frameCount <= 0.0 || framesPerSecond <= 0.0) {
    return grapple::foundation::Error{"desktop.video_metadata_invalid", "Video file metadata is incomplete for " + path.value + "."};
  }

  const std::filesystem::path filesystemPath{path.value};
  return grapple::asset::Asset{
    assetId,
    filesystemPath.stem().string(),
    grapple::asset::AssetMetadata{
      grapple::asset::AssetMediaType::Video,
      path,
      std::nullopt,
      grapple::foundation::TimeSeconds{frameCount / framesPerSecond},
      grapple::foundation::Resolution{width, height},
      grapple::foundation::FrameRate{static_cast<std::int32_t>(framesPerSecond * 1000.0), 1000}
    }
  };
}

class DesktopWindow final : public QMainWindow {
public:
  explicit DesktopWindow(grapple::app::NativeWorkspaceSession& workspace)
    : workspace_{workspace} {
    setWindowTitle("Grapple Native");
    resize(1180, 720);

    auto* root = new QWidget;
    auto* layout = new QGridLayout{root};
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(16);

    summary_ = new QLabel;
    summary_->setObjectName("summary");
    summary_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    mediaBin_ = new QTextEdit;
    mediaBin_->setObjectName("mediaBin");
    mediaBin_->setReadOnly(true);

    previewFrame_ = new QFrame;
    previewFrame_->setObjectName("previewFrame");
    previewFrame_->setMinimumSize(520, 320);
    auto* previewLayout = new QVBoxLayout{previewFrame_};
    previewTitle_ = new QLabel{"Preview"};
    previewTitle_->setObjectName("panelTitle");
    previewSurface_ = new PreviewSurface;
    previewLayout->addWidget(previewTitle_);
    previewLayout->addWidget(previewSurface_, 1);

    timeline_ = new TimelinePanel;

    inspector_ = new QTextEdit;
    inspector_->setObjectName("inspector");
    inspector_->setReadOnly(true);

    log_ = new QTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);

    playbackTimer_ = new QTimer{this};
    playbackTimer_->setInterval(33);
    connect(playbackTimer_, &QTimer::timeout, this, [this] { advancePlaybackFrame(); });

    auto* refreshButton = new QPushButton{"Refresh Preview"};
    playheadLabel_ = new QLabel;
    playheadLabel_->setObjectName("playheadLabel");
    auto* playButton = new QPushButton{"Play"};
    auto* pauseButton = new QPushButton{"Pause"};
    auto* seekStartButton = new QPushButton{"Seek Start"};
    auto* stepBackButton = new QPushButton{"Step -1s"};
    auto* stepForwardButton = new QPushButton{"Step +1s"};
    auto* importVideoButton = new QPushButton{"Import Video"};
    auto* openPackageButton = new QPushButton{"Open Package"};
    auto* addTrackButton = new QPushButton{"Add Track"};
    auto* deleteClipButton = new QPushButton{"Delete Clip"};
    auto* exportButton = new QPushButton{"Export Smoke"};
    auto* saveButton = new QPushButton{"Save Package"};
    auto* actionColumn = new QVBoxLayout;
    actionColumn->addWidget(refreshButton);
    actionColumn->addWidget(playheadLabel_);
    actionColumn->addWidget(playButton);
    actionColumn->addWidget(pauseButton);
    actionColumn->addWidget(seekStartButton);
    actionColumn->addWidget(stepBackButton);
    actionColumn->addWidget(stepForwardButton);
    actionColumn->addWidget(importVideoButton);
    actionColumn->addWidget(openPackageButton);
    actionColumn->addWidget(addTrackButton);
    actionColumn->addWidget(deleteClipButton);
    actionColumn->addWidget(exportButton);
    actionColumn->addWidget(saveButton);
    actionColumn->addStretch(1);

    auto* actions = new QWidget;
    actions->setObjectName("actions");
    actions->setLayout(actionColumn);

    auto* leftPanel = new QWidget;
    auto* leftLayout = new QVBoxLayout{leftPanel};
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(16);
    leftLayout->addWidget(summary_);
    leftLayout->addWidget(mediaBin_, 1);

    layout->addWidget(leftPanel, 0, 0, 2, 1);
    layout->addWidget(previewFrame_, 0, 1, 2, 1);
    auto* sidePanel = new QWidget;
    auto* sideLayout = new QVBoxLayout{sidePanel};
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(16);
    sideLayout->addWidget(inspector_, 1);
    sideLayout->addWidget(log_, 1);

    layout->addWidget(actions, 0, 2, 1, 1);
    layout->addWidget(sidePanel, 1, 2, 1, 1);
    layout->addWidget(timeline_, 2, 0, 1, 3);
    layout->setColumnStretch(0, 2);
    layout->setColumnStretch(1, 4);
    layout->setColumnStretch(2, 2);
    layout->setRowStretch(0, 1);
    layout->setRowStretch(1, 1);
    layout->setRowStretch(2, 1);
    setCentralWidget(root);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshPreview(); });
    connect(playButton, &QPushButton::clicked, this, [this] { startPlayback(); });
    connect(pauseButton, &QPushButton::clicked, this, [this] { pausePlayback(); });
    connect(seekStartButton, &QPushButton::clicked, this, [this] { seekTo(grapple::foundation::TimeSeconds{0.0}); });
    connect(stepBackButton, &QPushButton::clicked, this, [this] { stepPlayhead(-1.0); });
    connect(stepForwardButton, &QPushButton::clicked, this, [this] { stepPlayhead(1.0); });
    connect(importVideoButton, &QPushButton::clicked, this, [this] { chooseAndImportVideo(); });
    connect(openPackageButton, &QPushButton::clicked, this, [this] { chooseAndOpenPackage(); });
    connect(addTrackButton, &QPushButton::clicked, this, [this] { addTrack(); });
    connect(deleteClipButton, &QPushButton::clicked, this, [this] { deleteSelectedClip(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { runExport(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { savePackage(); });
    timeline_->setSeekHandler([this](grapple::foundation::TimeSeconds time) { seekTo(time); });
    timeline_->setSelectionHandler([this](grapple::foundation::NodeId nodeId) { selectNode(std::move(nodeId)); });

    setStyleSheet(R"(
      QMainWindow { background: #15171c; color: #e9edf5; }
      QWidget { background: #15171c; color: #e9edf5; font-family: "DejaVu Sans"; font-size: 14px; }
      QLabel#summary, QTextEdit#mediaBin, QTextEdit#timeline, QTextEdit#inspector, QTextEdit#log, QWidget#actions {
        background: #20242d; border: 1px solid #343b4a; border-radius: 10px; padding: 12px;
      }
      QTextEdit#mediaBin { color: #dce8f6; }
      QTextEdit#inspector { color: #eaf3ff; }
      QTextEdit#log { color: #b8c7dc; }
      QFrame#previewFrame {
        background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0b0e14, stop:1 #17202e);
        border: 1px solid #3c526f; border-radius: 12px;
      }
      QLabel#panelTitle { color: #9fb7d5; font-weight: 700; letter-spacing: 1px; }
      QLabel#playheadLabel { color: #d8f3ff; font-weight: 700; padding: 6px 0; }
      QPushButton {
        background: #58c7d8; color: #071015; border: 0; border-radius: 8px; padding: 10px 14px; font-weight: 700;
      }
      QPushButton:hover { background: #79ddea; }
    )");

    refreshViewModel();
    refreshPreview();
  }

  void refreshViewModel() {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    summary_->setText(summaryText(viewModel.value()));
    mediaBin_->setPlainText(mediaBinText(viewModel.value()));
    timeline_->setViewModel(viewModel.value());
    timeline_->setPlayhead(workspace_.preview().state().playhead);
    timeline_->setSelectedNodeId(selectedNodeId_);
    updateInspector(viewModel.value());
    timelineDuration_ = viewModel.value().timeline.duration;
  }

  void refreshPreview() {
    const auto refresh = workspace_.preview().refreshFromProject();
    if (!refresh) {
      appendError(refresh.error());
      return;
    }
    renderCurrentFrame();
    log_->append(QString{"Preview refreshed at %1"}.arg(qString(refresh.value().revision.value())));
  }

  void renderCurrentFrame() {
    const grapple::render::PreviewRenderShellState previewState = workspace_.preview().state();
    const auto frame = workspace_.preview().renderFrame(grapple::render::RenderFrameRequest{
      previewState.playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!frame) {
      appendError(frame.error());
      return;
    }
    previewSurface_->setFrame(frame.value().frame);
    playheadLabel_->setText(QString{"Playhead: %1"}.arg(timeText(previewState.playhead)));
    timeline_->setPlayhead(previewState.playhead);
  }

  void seekTo(grapple::foundation::TimeSeconds time) {
    const auto seek = workspace_.preview().seek(time);
    if (!seek) {
      appendError(seek.error());
      return;
    }
    renderCurrentFrame();
  }

  void stepPlayhead(double deltaSeconds) {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    const double duration = std::max(0.0, viewModel.value().timeline.duration.value);
    const double current = workspace_.preview().state().playhead.value;
    seekTo(grapple::foundation::TimeSeconds{std::clamp(current + deltaSeconds, 0.0, duration)});
  }

  void clickTimelineAtRatio(double ratio) {
    const int left = 150;
    const int right = std::max(left + 1, timeline_->width() - 16);
    const int x = left + static_cast<int>(std::clamp(ratio, 0.0, 1.0) * static_cast<double>(right - left));
    QMouseEvent event{
      QEvent::MouseButtonPress,
      QPointF{static_cast<double>(x), 20.0},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier
    };
    QApplication::sendEvent(timeline_, &event);
  }

  void clickFirstTimelineClip() {
    QMouseEvent event{
      QEvent::MouseButtonPress,
      QPointF{180.0, 56.0},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier
    };
    QApplication::sendEvent(timeline_, &event);
  }

  std::optional<grapple::foundation::NodeId> selectedNodeId() const {
    return selectedNodeId_;
  }

  void startPlayback() {
    const auto play = workspace_.preview().play();
    if (!play) {
      appendError(play.error());
      return;
    }

    playbackTimer_->start();
    renderCurrentFrame();
  }

  void pausePlayback() {
    playbackTimer_->stop();
    const auto pause = workspace_.preview().pause();
    if (!pause) {
      appendError(pause.error());
      return;
    }

    renderCurrentFrame();
  }

  void advancePlaybackFrame() {
    if (workspace_.preview().state().playback != grapple::render::PreviewPlaybackState::Playing) {
      playbackTimer_->stop();
      return;
    }

    const double duration = std::max(0.0, timelineDuration_.value);
    const double next = workspace_.preview().state().playhead.value + (1.0 / 30.0);
    if (duration <= 0.0 || next >= duration) {
      seekTo(grapple::foundation::TimeSeconds{duration});
      pausePlayback();
      return;
    }

    seekTo(grapple::foundation::TimeSeconds{next});
  }

  void addTrack() {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.compositions.empty()) {
      appendError(grapple::foundation::Error{"desktop.composition_missing", "Add Track requires a composition."});
      return;
    }

    const std::size_t trackNumber = viewModel.value().timeline.layers.size() + 1;
    const auto result = workspace_.commandWriter().apply(
      grapple::project::CreateTrackCommand{
        workspace_.commandWriter().nextNodeId("track"),
        viewModel.value().timeline.compositions[0].sourceNodeId,
        workspace_.commandWriter().nextEdgeId("contains_track"),
        "Video " + std::to_string(trackNumber)
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Added track at %1"}.arg(qString(result.value().snapshot.revision.value())));
  }

  void importVideoFile(const grapple::foundation::FilePath& path) {
    const auto asset = inspectVideoAsset(workspace_.commandWriter().nextAssetId(std::filesystem::path{path.value}.stem().string()), path);
    if (!asset) {
      appendError(asset.error());
      return;
    }
    const grapple::asset::Asset& videoAsset = asset.value();
    const grapple::foundation::TimeSeconds videoDuration = videoAsset.metadata.duration.value();

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.layers.empty()) {
      appendError(grapple::foundation::Error{"desktop.track_missing", "Import Video requires a timeline track."});
      return;
    }

    const auto registeredAsset = workspace_.commandWriter().apply(
      grapple::project::RegisterAssetCommand{videoAsset},
      importerSource()
    );
    if (!registeredAsset) {
      appendError(registeredAsset.error());
      return;
    }

    const auto registeredSource = workspace_.mediaSources().registerSource(grapple::media::MediaSource{
      videoAsset.id,
      grapple::media::MediaSourceKind::Video,
      path
    });
    if (!registeredSource) {
      appendError(registeredSource.error());
      return;
    }

    const auto clip = workspace_.commandWriter().apply(
      grapple::project::CreateClipCommand{
        workspace_.commandWriter().nextNodeId("clip"),
        viewModel.value().timeline.layers.front().sourceNodeId,
        workspace_.commandWriter().nextEdgeId("contains_clip"),
        grapple::timeline::ClipPayload{
          grapple::timeline::ClipKind::Video,
          grapple::foundation::TimeRange{
            viewModel.value().timeline.duration,
            grapple::foundation::TimeSeconds{viewModel.value().timeline.duration.value + videoDuration.value}
          },
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            videoDuration
          },
          1.0,
          videoAsset.id,
          grapple::timeline::Transform{}
        },
        static_cast<std::int64_t>(viewModel.value().timeline.clips.size())
      },
      userSource()
    );
    if (!clip) {
      appendError(clip.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Imported %1"}.arg(qString(videoAsset.name)));
  }

  void deleteSelectedClip() {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Delete Clip requires a selected clip."});
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const auto selectedClip = std::find_if(
      viewModel.value().timeline.clips.begin(),
      viewModel.value().timeline.clips.end(),
      [&](const grapple::app::AppClipRow& clip) {
        return clip.sourceNodeId == selectedNodeId_.value();
      }
    );
    if (selectedClip == viewModel.value().timeline.clips.end()) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_clip", "Delete Clip only applies to selected clips."});
      return;
    }

    const auto deleted = workspace_.commandWriter().apply(
      grapple::project::DeleteClipCommand{selectedClip->sourceNodeId},
      userSource()
    );
    if (!deleted) {
      appendError(deleted.error());
      return;
    }

    selectedNodeId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Deleted clip at %1"}.arg(qString(deleted.value().snapshot.revision.value())));
  }

  void runExport() {
    const auto prepare = workspace_.exportSession().prepareFromProject();
    if (!prepare) {
      appendError(prepare.error());
      return;
    }
    const auto result = workspace_.exportSession().render(grapple::render::ExportSettings{
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, grapple::foundation::TimeSeconds{1.0}},
      grapple::foundation::FrameRate{2, 1},
      grapple::foundation::Resolution{1920, 1080},
      grapple::render::Codec{"test"},
      grapple::render::RenderQuality::Final,
      grapple::foundation::FilePath{"/tmp/grapple-desktop-export.mov"}
    });
    if (!result) {
      appendError(result.error());
      return;
    }
    log_->append(QString{"Export evaluated %1 frames -> %2"}
      .arg(result.value().framesEvaluated)
      .arg(qString(result.value().outputPath.value)));
  }

  void savePackage() {
    const auto write = workspace_.project().writePackage();
    if (!write) {
      appendError(write.error());
      return;
    }
    log_->append(QString{"Package saved\n%1\n%2\n%3\n%4"}
      .arg(qString(write.value().snapshotPath.value))
      .arg(qString(write.value().manifestPath.value))
      .arg(qString(write.value().commandLogPath.value))
      .arg(qString(write.value().eventLogPath.value)));
  }

  void openPackageRoot(const grapple::foundation::FilePath& rootPath) {
    pausePlayback();
    const auto opened = workspace_.openPackageRootInPlace(rootPath);
    if (!opened) {
      appendError(opened.error());
      return;
    }

    selectedNodeId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Opened package %1"}.arg(qString(rootPath.value)));
  }

private:
  void appendError(const grapple::foundation::Error& error) {
    log_->append(QString{"%1: %2"}.arg(qString(error.code)).arg(qString(error.message)));
  }

  void selectNode(grapple::foundation::NodeId nodeId) {
    selectedNodeId_ = std::move(nodeId);
    timeline_->setSelectedNodeId(selectedNodeId_);

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    updateInspector(viewModel.value());
  }

  void updateInspector(const grapple::app::AppViewModel& viewModel) {
    inspector_->setPlainText(inspectorText(viewModel, selectedNodeId_));
  }

  void chooseAndImportVideo() {
    const QString path = QFileDialog::getOpenFileName(this, "Import Video", QString{}, "Video Files (*.mov *.mp4 *.avi *.mkv)");
    if (path.isEmpty()) {
      return;
    }
    importVideoFile(grapple::foundation::FilePath{path.toStdString()});
  }

  void chooseAndOpenPackage() {
    const QString path = QFileDialog::getExistingDirectory(this, "Open Package");
    if (path.isEmpty()) {
      return;
    }
    openPackageRoot(grapple::foundation::FilePath{path.toStdString()});
  }

  grapple::app::NativeWorkspaceSession& workspace_;
  QLabel* summary_ = nullptr;
  QTextEdit* mediaBin_ = nullptr;
  QLabel* previewTitle_ = nullptr;
  QLabel* playheadLabel_ = nullptr;
  PreviewSurface* previewSurface_ = nullptr;
  TimelinePanel* timeline_ = nullptr;
  QTextEdit* inspector_ = nullptr;
  QTextEdit* log_ = nullptr;
  QFrame* previewFrame_ = nullptr;
  QTimer* playbackTimer_ = nullptr;
  grapple::foundation::TimeSeconds timelineDuration_;
  std::optional<grapple::foundation::NodeId> selectedNodeId_;
};

} // namespace

int main(int argc, char* argv[]) {
  bool smoke = false;
  bool mutateSmoke = false;
  bool seekSmoke = false;
  bool timelineSeekSmoke = false;
  bool selectSmoke = false;
  bool importSmoke = false;
  bool deleteSmoke = false;
  bool playbackSmoke = false;
  bool openPackageSmoke = false;
  std::optional<std::string> screenshotPath;
  for (int index = 1; index < argc; ++index) {
    const std::string argument{argv[index]};
    if (argument == "--smoke") {
      smoke = true;
    } else if (argument == "--mutate-smoke") {
      mutateSmoke = true;
    } else if (argument == "--seek-smoke") {
      seekSmoke = true;
    } else if (argument == "--timeline-seek-smoke") {
      timelineSeekSmoke = true;
    } else if (argument == "--select-smoke") {
      selectSmoke = true;
    } else if (argument == "--import-smoke") {
      importSmoke = true;
    } else if (argument == "--delete-smoke") {
      deleteSmoke = true;
    } else if (argument == "--playback-smoke") {
      playbackSmoke = true;
    } else if (argument == "--open-package-smoke") {
      openPackageSmoke = true;
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else {
      std::cerr << "Expected --smoke, --mutate-smoke, --seek-smoke, --timeline-seek-smoke, --select-smoke, --import-smoke, --delete-smoke, --playback-smoke, --open-package-smoke, or --screenshot <path>.\n";
      return 1;
    }
  }

  QApplication app{argc, argv};
  grapple::app::NativeProjectSession session{
    grapple::foundation::ProjectId{"proj_desktop"},
    "Desktop Demo",
    grapple::storage::ProjectPackage{
      grapple::foundation::ProjectId{"proj_desktop"},
      grapple::foundation::FilePath{"/tmp/grapple-desktop-package"},
      1
    }
  };
  const auto demoVideo = ensureDemoVideoFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/walking-woman.avi"});
  if (!demoVideo) {
    printError(demoVideo.error());
    return 1;
  }

  const auto populated = populateDemo(session, true);
  if (!populated) {
    printError(populated.error());
    return 1;
  }

  auto workspace = grapple::app::NativeWorkspaceSession::fromProject(std::move(session));
  if (!workspace) {
    printError(workspace.error());
    return 1;
  }

  DesktopWindow window{workspace.value()};

  if (smoke) {
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    return 0;
  }

  if (mutateSmoke) {
    window.addTrack();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "layers=" << viewModel.value().timeline.layers.size() << '\n';
    return viewModel.value().timeline.layers.size() == 2 ? 0 : 1;
  }

  if (seekSmoke) {
    window.seekTo(grapple::foundation::TimeSeconds{5.0});
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead == grapple::foundation::TimeSeconds{5.0} ? 0 : 1;
  }

  if (timelineSeekSmoke) {
    window.show();
    app.processEvents();
    window.clickTimelineAtRatio(0.5);
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead.value > 4.9 && previewState.playhead.value < 5.1 ? 0 : 1;
  }

  if (selectSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    const auto selectedNodeId = window.selectedNodeId();
    if (!selectedNodeId.has_value()) {
      std::cerr << "No selected timeline node.\n";
      return 1;
    }
    std::cout << "selected=" << selectedNodeId->value() << '\n';
    return selectedNodeId.value() == grapple::foundation::NodeId{"node_clip_3"} ? 0 : 1;
  }

  if (importSmoke) {
    window.importVideoFile(grapple::foundation::FilePath{"/tmp/grapple-native-demo/walking-woman.avi"});
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "assets=" << viewModel.value().assets.count << '\n';
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    std::cout << "duration=" << viewModel.value().timeline.duration.value << '\n';
    return viewModel.value().assets.count == 2 &&
           viewModel.value().timeline.clips.size() == 2 &&
           viewModel.value().timeline.duration.value > 19.9
      ? 0
      : 1;
  }

  if (deleteSmoke) {
    window.show();
    app.processEvents();
    window.clickFirstTimelineClip();
    window.deleteSelectedClip();
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "clips=" << viewModel.value().timeline.clips.size() << '\n';
    return viewModel.value().timeline.clips.empty() ? 0 : 1;
  }

  if (playbackSmoke) {
    window.startPlayback();
    window.advancePlaybackFrame();
    window.pausePlayback();
    const grapple::render::PreviewRenderShellState previewState = workspace.value().preview().state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead.value > 0.0 ? 0 : 1;
  }

  if (openPackageSmoke) {
    const auto write = workspace.value().project().writePackage();
    if (!write) {
      printError(write.error());
      return 1;
    }
    window.openPackageRoot(grapple::foundation::FilePath{"/tmp/grapple-desktop-package"});
    const auto viewModel = workspace.value().project().buildViewModel();
    if (!viewModel) {
      printError(viewModel.error());
      return 1;
    }
    std::cout << "project=" << viewModel.value().project.projectId.value() << '\n';
    std::cout << "revision=" << viewModel.value().project.revision.value() << '\n';
    std::cout << "commands=" << workspace.value().project().packageState().commandLog.records().size() << '\n';
    return viewModel.value().project.projectId == grapple::foundation::ProjectId{"proj_desktop"} &&
           viewModel.value().project.revision == grapple::foundation::RevisionId{"rev_6"} &&
           workspace.value().project().packageState().commandLog.records().size() == 6
      ? 0
      : 1;
  }

  if (screenshotPath.has_value()) {
    window.show();
    app.processEvents();
    const QPixmap pixmap = window.grab();
    if (!pixmap.save(QString::fromStdString(*screenshotPath))) {
      std::cerr << "Could not write screenshot: " << *screenshotPath << '\n';
      return 1;
    }
    return 0;
  }

  window.show();
  return app.exec();
}
