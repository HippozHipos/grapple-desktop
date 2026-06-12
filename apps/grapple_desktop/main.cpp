#include <DemoProject.hpp>

#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/app/NativeProjectSession.hpp>

#include <QApplication>
#include <QColor>
#include <QFrame>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

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

QString mediaKindText(grapple::render::RenderedMediaKind kind) {
  switch (kind) {
    case grapple::render::RenderedMediaKind::Video:
      return "Video";
    case grapple::render::RenderedMediaKind::Image:
      return "Image";
  }

  return "Unknown";
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
      drawLayerRow(painter, viewModel, layer, QRect{0, y, width(), rowHeight}, left, trackWidth, duration);
      y += rowHeight;
    }

    if (!viewModel.timeline.cameras.empty()) {
      drawCameraRow(painter, viewModel, QRect{0, y, width(), rowHeight}, left, trackWidth);
    }

    drawPlayhead(painter, left, trackWidth, duration);
  }

private:
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
    int left,
    int trackWidth,
    double duration
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
      const int x = clipX(clip.timelineRange.start, left, trackWidth, duration);
      const int endX = clipX(clip.timelineRange.end, left, trackWidth, duration);
      const QRect clipRect{x + 2, row.top() + 8, std::max(18, endX - x - 4), row.height() - 16};
      painter.setPen(QColor{"#b9c7f0"});
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
    int left,
    int trackWidth
  ) const {
    painter.fillRect(row, QColor{"#1f2c33"});
    painter.setPen(QColor{"#3b5964"});
    painter.drawLine(0, row.bottom(), width(), row.bottom());
    painter.setPen(QColor{"#d7f8ff"});
    painter.drawText(QRect{16, row.top(), left - 28, row.height()}, Qt::AlignVCenter | Qt::AlignLeft, "Cameras");

    const QRect cameraStrip{left + 2, row.top() + 10, std::max(18, trackWidth - 4), row.height() - 20};
    painter.setPen(QColor{"#86e8f2"});
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

class DesktopWindow final : public QMainWindow {
public:
  DesktopWindow(
    grapple::app::NativeProjectSession& session,
    grapple::app::NativePreviewSession& preview,
    grapple::app::NativeExportSession& exportSession
  ) : session_{session},
      preview_{preview},
      exportSession_{exportSession},
      commandWriter_{session} {
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

    log_ = new QTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);

    auto* refreshButton = new QPushButton{"Refresh Preview"};
    playheadLabel_ = new QLabel;
    playheadLabel_->setObjectName("playheadLabel");
    auto* seekStartButton = new QPushButton{"Seek Start"};
    auto* stepBackButton = new QPushButton{"Step -1s"};
    auto* stepForwardButton = new QPushButton{"Step +1s"};
    auto* addTrackButton = new QPushButton{"Add Track"};
    auto* exportButton = new QPushButton{"Export Smoke"};
    auto* saveButton = new QPushButton{"Save Package"};
    auto* actionColumn = new QVBoxLayout;
    actionColumn->addWidget(refreshButton);
    actionColumn->addWidget(playheadLabel_);
    actionColumn->addWidget(seekStartButton);
    actionColumn->addWidget(stepBackButton);
    actionColumn->addWidget(stepForwardButton);
    actionColumn->addWidget(addTrackButton);
    actionColumn->addWidget(exportButton);
    actionColumn->addWidget(saveButton);
    actionColumn->addStretch(1);

    auto* actions = new QWidget;
    actions->setObjectName("actions");
    actions->setLayout(actionColumn);

    layout->addWidget(summary_, 0, 0, 1, 1);
    layout->addWidget(previewFrame_, 0, 1, 2, 1);
    layout->addWidget(actions, 0, 2, 1, 1);
    layout->addWidget(log_, 1, 2, 1, 1);
    layout->addWidget(timeline_, 2, 0, 1, 3);
    layout->setColumnStretch(0, 2);
    layout->setColumnStretch(1, 4);
    layout->setColumnStretch(2, 2);
    layout->setRowStretch(0, 1);
    layout->setRowStretch(1, 1);
    layout->setRowStretch(2, 1);
    setCentralWidget(root);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshPreview(); });
    connect(seekStartButton, &QPushButton::clicked, this, [this] { seekTo(grapple::foundation::TimeSeconds{0.0}); });
    connect(stepBackButton, &QPushButton::clicked, this, [this] { stepPlayhead(-1.0); });
    connect(stepForwardButton, &QPushButton::clicked, this, [this] { stepPlayhead(1.0); });
    connect(addTrackButton, &QPushButton::clicked, this, [this] { addTrack(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { runExport(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { savePackage(); });

    setStyleSheet(R"(
      QMainWindow { background: #15171c; color: #e9edf5; }
      QWidget { background: #15171c; color: #e9edf5; font-family: "DejaVu Sans"; font-size: 14px; }
      QLabel#summary, QTextEdit#timeline, QTextEdit#log, QWidget#actions {
        background: #20242d; border: 1px solid #343b4a; border-radius: 10px; padding: 12px;
      }
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
    const auto viewModel = session_.buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    summary_->setText(summaryText(viewModel.value()));
    timeline_->setViewModel(viewModel.value());
    timeline_->setPlayhead(preview_.state().playhead);
  }

  void refreshPreview() {
    const auto refresh = preview_.refreshFromProject();
    if (!refresh) {
      appendError(refresh.error());
      return;
    }
    renderCurrentFrame();
    log_->append(QString{"Preview refreshed at %1"}.arg(qString(refresh.value().revision.value())));
  }

  void renderCurrentFrame() {
    const grapple::render::PreviewRenderShellState previewState = preview_.state();
    const auto frame = preview_.renderFrame(grapple::render::RenderFrameRequest{
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
    const auto seek = preview_.seek(time);
    if (!seek) {
      appendError(seek.error());
      return;
    }
    renderCurrentFrame();
  }

  void stepPlayhead(double deltaSeconds) {
    const auto viewModel = session_.buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    const double duration = std::max(0.0, viewModel.value().timeline.duration.value);
    const double current = preview_.state().playhead.value;
    seekTo(grapple::foundation::TimeSeconds{std::clamp(current + deltaSeconds, 0.0, duration)});
  }

  void addTrack() {
    const auto viewModel = session_.buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.compositions.empty()) {
      appendError(grapple::foundation::Error{"desktop.composition_missing", "Add Track requires a composition."});
      return;
    }

    const std::size_t trackNumber = viewModel.value().timeline.layers.size() + 1;
    const auto result = commandWriter_.apply(
      grapple::project::CreateTrackCommand{
        commandWriter_.nextNodeId("track"),
        viewModel.value().timeline.compositions[0].sourceNodeId,
        commandWriter_.nextEdgeId("contains_track"),
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

  void runExport() {
    const auto prepare = exportSession_.prepareFromProject();
    if (!prepare) {
      appendError(prepare.error());
      return;
    }
    const auto result = exportSession_.render(grapple::render::ExportSettings{
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
    const auto write = session_.writePackage();
    if (!write) {
      appendError(write.error());
      return;
    }
    log_->append(QString{"Package saved\n%1\n%2"}
      .arg(qString(write.value().snapshotPath.value))
      .arg(qString(write.value().manifestPath.value)));
  }

private:
  void appendError(const grapple::foundation::Error& error) {
    log_->append(QString{"%1: %2"}.arg(qString(error.code)).arg(qString(error.message)));
  }

  grapple::app::NativeProjectSession& session_;
  grapple::app::NativePreviewSession& preview_;
  grapple::app::NativeExportSession& exportSession_;
  grapple::app::NativeProjectCommandWriter commandWriter_;
  QLabel* summary_ = nullptr;
  QLabel* previewTitle_ = nullptr;
  QLabel* playheadLabel_ = nullptr;
  PreviewSurface* previewSurface_ = nullptr;
  TimelinePanel* timeline_ = nullptr;
  QTextEdit* log_ = nullptr;
  QFrame* previewFrame_ = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
  bool smoke = false;
  bool mutateSmoke = false;
  bool seekSmoke = false;
  std::optional<std::string> screenshotPath;
  for (int index = 1; index < argc; ++index) {
    const std::string argument{argv[index]};
    if (argument == "--smoke") {
      smoke = true;
    } else if (argument == "--mutate-smoke") {
      mutateSmoke = true;
    } else if (argument == "--seek-smoke") {
      seekSmoke = true;
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else {
      std::cerr << "Expected --smoke, --mutate-smoke, --seek-smoke, or --screenshot <path>.\n";
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
  const auto populated = populateDemo(session, true);
  if (!populated) {
    printError(populated.error());
    return 1;
  }

  grapple::app::NativePreviewSession preview{session};
  grapple::app::NativeExportSession exportSession{session};
  DesktopWindow window{session, preview, exportSession};

  if (smoke) {
    const auto viewModel = session.buildViewModel();
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
    const auto viewModel = session.buildViewModel();
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
    const grapple::render::PreviewRenderShellState previewState = preview.state();
    std::cout << "playhead=" << previewState.playhead.value << '\n';
    return previewState.playhead == grapple::foundation::TimeSeconds{5.0} ? 0 : 1;
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
