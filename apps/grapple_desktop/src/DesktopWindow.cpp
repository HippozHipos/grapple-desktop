#include "DesktopWindow.hpp"

#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/graph/GraphNode.hpp>
#include <grapple/media/MediaSource.hpp>
#include <grapple/render/RenderDiagnostic.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/ui_qt/EffectParamPanel.hpp>
#include <grapple/ui_qt/PreviewSurface.hpp>
#include <grapple/ui_qt/StewardPanel.hpp>
#include <grapple/ui_qt/TimelinePanel.hpp>

#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace {

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

QString inspectorText(
  const grapple::app::AppViewModel& viewModel,
  const std::optional<grapple::foundation::NodeId>& selectedNodeId,
  const std::optional<grapple::foundation::AssetId>& selectedAssetId
) {
  if (selectedAssetId.has_value()) {
    for (const grapple::app::AppAssetRow& asset : viewModel.assets.rows) {
      if (asset.assetId == selectedAssetId.value()) {
        QStringList lines{
          "Inspector",
          QString{"Asset %1"}.arg(qString(asset.assetId.value())),
          QString{"Name: %1"}.arg(qString(asset.name)),
          QString{"Type: %1"}.arg(qString(asset.mediaType))
        };
        if (asset.duration.has_value()) {
          lines << QString{"Duration: %1"}.arg(timeText(*asset.duration));
        }
        if (asset.dimensions.has_value()) {
          lines << QString{"Dimensions: %1x%2"}.arg(asset.dimensions->width).arg(asset.dimensions->height);
        }
        lines << QString{"Source: %1"}.arg(qString(asset.sourcePath.value));
        return lines.join('\n');
      }
    }
  }

  if (!selectedNodeId.has_value()) {
    return "Inspector\nNo selection";
  }

  auto attachedEffectsText = [&](const grapple::foundation::NodeId& targetNodeId) {
    QStringList lines;
    for (const grapple::app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
      if (graph.targetNodeId != targetNodeId) {
        continue;
      }
      for (const grapple::app::AppEffectRow& effect : graph.effects) {
        lines << QString{"Effect: %1 [%2]"}
          .arg(qString(effect.displayName))
          .arg(qString(effect.implementationKind));
        if (!effect.entrypoint.empty()) {
          lines << QString{"Entrypoint: %1"}.arg(qString(effect.entrypoint));
        }
        lines << QString{"Range: %1 - %2"}
          .arg(timeText(effect.activeRange.start))
          .arg(timeText(effect.activeRange.end));
        if (!effect.params.empty()) {
          QStringList params;
          for (const grapple::app::AppEffectParamRow& param : effect.params) {
            const QString displayName = param.label.empty()
              ? qString(param.name)
              : QString{"%1 (%2)"}.arg(qString(param.label)).arg(qString(param.name));
            QString paramText = QString{"%1=%2"}.arg(displayName).arg(qString(param.value));
            if (param.numericMin.has_value() && param.numericMax.has_value()) {
              paramText += QString{" [%1..%2"}.arg(*param.numericMin).arg(*param.numericMax);
              if (param.numericStep.has_value()) {
                paramText += QString{", step %1"}.arg(*param.numericStep);
              }
              paramText += ']';
            }
            params << paramText;
          }
          lines << QString{"Params: %1"}.arg(params.join(", "));
        }
      }
    }

    if (lines.empty()) {
      return QString{"Effects: none"};
    }
    return QString{"Effects\n%1"}.arg(lines.join('\n'));
  };

  for (const grapple::app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nClip %1\nAsset: %2\nTrack: %3\nRange: %4s - %5s\n\n%6"}
        .arg(qString(clip.sourceNodeId.value()))
        .arg(qString(clip.assetId.value()))
        .arg(qString(clip.trackNodeId.value()))
        .arg(clip.timelineRange.start.value)
        .arg(clip.timelineRange.end.value)
        .arg(attachedEffectsText(clip.sourceNodeId));
    }
  }

  for (const grapple::app::AppCameraRow& camera : viewModel.timeline.cameras) {
    if (camera.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nCamera %1\nName: %2\n\n%3"}
        .arg(qString(camera.sourceNodeId.value()))
        .arg(qString(camera.name))
        .arg(attachedEffectsText(camera.sourceNodeId));
    }
  }

  for (const grapple::app::AppLayerRow& layer : viewModel.timeline.layers) {
    if (layer.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nLayer %1\nClips: %2\n\n%3"}
        .arg(qString(layer.name))
        .arg(layer.clipCount)
        .arg(attachedEffectsText(layer.sourceNodeId));
    }
  }

  return QString{"Inspector\nUnknown node %1"}.arg(qString(selectedNodeId->value()));
}

QString runtimeSeverityText(grapple::runtime::DiagnosticSeverity severity) {
  switch (severity) {
    case grapple::runtime::DiagnosticSeverity::Info:
      return "info";
    case grapple::runtime::DiagnosticSeverity::Warning:
      return "warning";
    case grapple::runtime::DiagnosticSeverity::Error:
      return "error";
  }

  return "unknown";
}

QString renderSeverityText(grapple::render::DiagnosticSeverity severity) {
  switch (severity) {
    case grapple::render::DiagnosticSeverity::Info:
      return "info";
    case grapple::render::DiagnosticSeverity::Warning:
      return "warning";
    case grapple::render::DiagnosticSeverity::Error:
      return "error";
  }

  return "unknown";
}

QString runtimeDiagnosticText(const grapple::runtime::RuntimeDiagnostic& diagnostic) {
  const QString node = diagnostic.location.nodeId.has_value()
    ? QString{" node=%1"}.arg(qString(diagnostic.location.nodeId->value()))
    : QString{};
  return QString{"Runtime diagnostic [%1] %2%3: %4"}
    .arg(runtimeSeverityText(diagnostic.severity))
    .arg(qString(diagnostic.code))
    .arg(node)
    .arg(qString(diagnostic.message));
}

QString renderDiagnosticText(const grapple::render::RenderDiagnostic& diagnostic) {
  const QString node = diagnostic.location.nodeId.has_value()
    ? QString{" node=%1"}.arg(qString(diagnostic.location.nodeId->value()))
    : QString{};
  return QString{"Render diagnostic [%1] %2%3: %4"}
    .arg(renderSeverityText(diagnostic.severity))
    .arg(qString(diagnostic.code))
    .arg(node)
    .arg(qString(diagnostic.message));
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

} // namespace

namespace grapple::desktop {

class DesktopWindowImpl final : public QMainWindow {
public:
  explicit DesktopWindowImpl(grapple::app::NativeWorkspaceSession& workspace)
    : workspace_{workspace} {
    setWindowTitle("Grapple");
    resize(1440, 860);

    auto* root = new QWidget;
    auto* layout = new QGridLayout{root};
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(12);

    summary_ = new QLabel;
    summary_->setObjectName("summary");
    summary_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    mediaBin_ = new QListWidget;
    mediaBin_->setObjectName("mediaBin");
    mediaBin_->setSelectionMode(QAbstractItemView::SingleSelection);

    previewFrame_ = new QFrame;
    previewFrame_->setObjectName("previewFrame");
    previewFrame_->setMinimumSize(620, 420);
    auto* previewLayout = new QVBoxLayout{previewFrame_};
    previewTitle_ = new QLabel{"Preview"};
    previewTitle_->setObjectName("panelTitle");
    previewSurface_ = new grapple::ui::PreviewSurface;
    previewLayout->addWidget(previewTitle_);
    previewLayout->addWidget(previewSurface_, 1);

    timeline_ = new grapple::ui::TimelinePanel;
    timeline_->setMinimumHeight(230);

    inspector_ = new QTextEdit;
    inspector_->setObjectName("inspector");
    inspector_->setReadOnly(true);

    effectParams_ = new grapple::ui::EffectParamPanel;
    effectParams_->setApplyHandler([this](
      grapple::foundation::NodeId effectNodeId,
      std::string paramName,
      double value
    ) {
      setEffectNumericParam(effectNodeId, paramName, value);
    });
    effectParams_->setDeleteHandler([this](grapple::foundation::NodeId effectNodeId) {
      deleteEffect(effectNodeId);
    });

    log_ = new QTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);

    steward_ = new grapple::ui::StewardPanel;

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
    auto* addMediaButton = new QPushButton{"Add Video To Timeline"};
    auto* openPackageButton = new QPushButton{"Open Package"};
    auto* addTrackButton = new QPushButton{"Add Track"};
    auto* moveClipButton = new QPushButton{"Move Clip +1s"};
    auto* deleteClipButton = new QPushButton{"Delete Clip"};
    auto* exportButton = new QPushButton{"Export Smoke"};
    auto* saveButton = new QPushButton{"Save Package"};
    auto* productTitle = new QLabel{"Grapple"};
    productTitle->setObjectName("productTitle");
    auto* productSubtitle = new QLabel{"Intent -> editable graph -> shared render core"};
    productSubtitle->setObjectName("productSubtitle");
    auto* titleBlock = new QWidget;
    auto* titleLayout = new QVBoxLayout{titleBlock};
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    titleLayout->addWidget(productTitle);
    titleLayout->addWidget(productSubtitle);

    auto* actions = new QWidget;
    actions->setObjectName("actions");
    auto* actionRow = new QHBoxLayout{actions};
    actionRow->setContentsMargins(10, 8, 10, 8);
    actionRow->setSpacing(8);
    actionRow->addWidget(titleBlock, 1);
    actionRow->addWidget(refreshButton);
    actionRow->addWidget(playheadLabel_);
    actionRow->addWidget(playButton);
    actionRow->addWidget(pauseButton);
    actionRow->addWidget(seekStartButton);
    actionRow->addWidget(stepBackButton);
    actionRow->addWidget(stepForwardButton);
    actionRow->addWidget(moveClipButton);
    actionRow->addWidget(deleteClipButton);
    actionRow->addWidget(exportButton);
    actionRow->addWidget(saveButton);

    auto* leftPanel = new QWidget;
    leftPanel->setObjectName("assetPanel");
    leftPanel->setMinimumWidth(240);
    auto* leftLayout = new QVBoxLayout{leftPanel};
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    auto* assetTitle = new QLabel{"Assets"};
    assetTitle->setObjectName("panelTitle");
    leftLayout->addWidget(assetTitle);
    leftLayout->addWidget(mediaBin_, 1);
    leftLayout->addWidget(importVideoButton);
    leftLayout->addWidget(addMediaButton);
    leftLayout->addWidget(openPackageButton);
    leftLayout->addWidget(addTrackButton);
    leftLayout->addWidget(summary_);

    auto* sidePanel = new QWidget;
    sidePanel->setObjectName("sidePanel");
    auto* sideLayout = new QVBoxLayout{sidePanel};
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(12);
    sideLayout->addWidget(steward_, 2);
    sideLayout->addWidget(inspector_, 1);
    sideLayout->addWidget(effectParams_);
    sideLayout->addWidget(log_, 1);

    layout->addWidget(actions, 0, 0, 1, 3);
    layout->addWidget(leftPanel, 1, 0, 1, 1);
    layout->addWidget(previewFrame_, 1, 1, 1, 1);
    layout->addWidget(sidePanel, 1, 2, 1, 1);
    layout->addWidget(timeline_, 2, 0, 1, 3);
    layout->setColumnStretch(0, 2);
    layout->setColumnStretch(1, 5);
    layout->setColumnStretch(2, 2);
    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 5);
    layout->setRowStretch(2, 2);
    setCentralWidget(root);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshPreview(); });
    connect(playButton, &QPushButton::clicked, this, [this] { startPlayback(); });
    connect(pauseButton, &QPushButton::clicked, this, [this] { pausePlayback(); });
    connect(seekStartButton, &QPushButton::clicked, this, [this] { seekTo(grapple::foundation::TimeSeconds{0.0}); });
    connect(stepBackButton, &QPushButton::clicked, this, [this] { stepPlayhead(-1.0); });
    connect(stepForwardButton, &QPushButton::clicked, this, [this] { stepPlayhead(1.0); });
    connect(importVideoButton, &QPushButton::clicked, this, [this] { chooseAndImportVideo(); });
    connect(addMediaButton, &QPushButton::clicked, this, [this] { addSelectedVideoToTimeline(); });
    connect(openPackageButton, &QPushButton::clicked, this, [this] { chooseAndOpenPackage(); });
    connect(addTrackButton, &QPushButton::clicked, this, [this] { addTrack(); });
    connect(moveClipButton, &QPushButton::clicked, this, [this] { moveSelectedClip(grapple::foundation::TimeSeconds{1.0}); });
    connect(deleteClipButton, &QPushButton::clicked, this, [this] { deleteSelectedClip(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { runExport(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { savePackage(); });
    steward_->setCreateCameraEffectHandler([this](std::string intent) { addEffectToSelectedTarget(std::move(intent)); });
    connect(mediaBin_, &QListWidget::currentRowChanged, this, [this](int row) { selectMediaAssetAtRow(row); });
    timeline_->setSeekHandler([this](grapple::foundation::TimeSeconds time) { seekTo(time); });
    timeline_->setSelectionHandler([this](grapple::foundation::NodeId nodeId) { selectNode(std::move(nodeId)); });

    setStyleSheet(R"(
      QMainWindow { background: #15171c; color: #e9edf5; }
      QWidget { background: #15171c; color: #e9edf5; font-family: "DejaVu Sans"; font-size: 14px; }
      QLabel#summary, QListWidget#mediaBin, QTextEdit#timeline, QTextEdit#inspector, QWidget#effectParams, QTextEdit#log, QWidget#actions {
        background: #20242d; border: 1px solid #343b4a; border-radius: 10px; padding: 12px;
      }
      QWidget#assetPanel, QWidget#sidePanel { background: transparent; }
      QWidget#stewardPanel, QTextEdit#stewardText {
        background: #20242d; border: 1px solid #343b4a; border-radius: 10px; color: #eaf3ff;
      }
      QListWidget#mediaBin { color: #dce8f6; outline: 0; }
      QListWidget#mediaBin::item { padding: 10px; border-radius: 8px; }
      QListWidget#mediaBin::item:selected { background: #36506f; color: #ffffff; }
      QTextEdit#inspector { color: #eaf3ff; }
      QTextEdit#log { color: #b8c7dc; }
      QFrame#previewFrame {
        background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0b0e14, stop:1 #17202e);
        border: 1px solid #3c526f; border-radius: 12px;
      }
      QLabel#panelTitle { color: #9fb7d5; font-weight: 700; letter-spacing: 1px; }
      QLabel#productTitle { color: #f2f7ff; font-size: 22px; font-weight: 900; }
      QLabel#productSubtitle { color: #9fb0c8; font-size: 12px; }
      QLabel#effectParamTitle { color: #e8f4ff; font-weight: 800; }
      QLabel#effectParamHelp { color: #9fb0c8; }
      QLabel#playheadLabel { color: #d8f3ff; font-weight: 800; padding: 0 8px; }
      QPushButton {
        background: #58c7d8; color: #071015; border: 0; border-radius: 8px; padding: 6px 10px; min-height: 24px; font-weight: 700;
      }
      QLineEdit, QDoubleSpinBox {
        background: #10141d; color: #e8f4ff; border: 1px solid #343b4a; border-radius: 8px; padding: 8px 10px;
      }
      QPushButton:hover { background: #79ddea; }
      QPushButton#effectParamDelete { background: #3a4658; color: #f4d4d4; }
      QPushButton#effectParamDelete:hover { background: #5a3841; }
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
    rebuildMediaBin(viewModel.value());
    steward_->setViewModel(viewModel.value());
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
    renderCurrentFrame(true);
    log_->append(QString{"Preview refreshed at %1"}.arg(qString(refresh.value().revision.value())));
  }

  void renderCurrentFrame(bool logDiagnostics = false) {
    const grapple::render::PreviewRenderShellState previewState = workspace_.preview().state();
    const auto frame = workspace_.preview().renderFrame(grapple::render::RenderFrameRequest{
      previewState.playhead,
      grapple::render::RenderQuality::Draft
    });
    if (!frame) {
      appendError(frame.error());
      return;
    }
    if (logDiagnostics) {
      appendDiagnostics(frame.value());
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

  void clickFirstTimelineCamera() {
    QMouseEvent event{
      QEvent::MouseButtonPress,
      QPointF{180.0, 100.0},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier
    };
    QApplication::sendEvent(timeline_, &event);
  }

  std::optional<grapple::foundation::NodeId> selectedNodeId() const {
    return selectedNodeId_;
  }

  std::optional<grapple::foundation::AssetId> selectedAssetId() const {
    return selectedAssetId_;
  }

  std::string inspectorContents() const {
    return inspector_->toPlainText().toStdString();
  }

  std::string logContents() const {
    return log_->toPlainText().toStdString();
  }

  std::string stewardContents() const {
    return steward_->contents();
  }

  void setStewardIntent(std::string intent) {
    steward_->setIntent(std::move(intent));
  }

  void clickStewardCreateCameraEffect() {
    steward_->triggerCreateCameraEffect();
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

    selectedNodeId_ = std::nullopt;
    selectedAssetId_ = videoAsset.id;
    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Imported %1"}.arg(qString(videoAsset.name)));
  }

  void addSelectedVideoToTimeline() {
    if (!selectedAssetId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.asset_selection_missing", "Add Video To Timeline requires a selected video asset."});
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.layers.empty()) {
      appendError(grapple::foundation::Error{"desktop.track_missing", "Add Video To Timeline requires a timeline track."});
      return;
    }

    const grapple::app::AppAssetRow* selectedAsset = nullptr;
    for (const grapple::app::AppAssetRow& asset : viewModel.value().assets.rows) {
      if (asset.assetId == selectedAssetId_.value()) {
        selectedAsset = &asset;
        break;
      }
    }
    if (selectedAsset == nullptr) {
      appendError(grapple::foundation::Error{"desktop.asset_selection_stale", "Selected media asset is not in the current project."});
      return;
    }
    if (selectedAsset->mediaType != "video") {
      appendError(grapple::foundation::Error{"desktop.asset_not_video", "Add Video To Timeline requires a video asset."});
      return;
    }
    if (!selectedAsset->duration.has_value()) {
      appendError(grapple::foundation::Error{"desktop.asset_duration_missing", "Selected media asset requires a duration before it can be placed on the timeline."});
      return;
    }

    const grapple::foundation::TimeSeconds duration = *selectedAsset->duration;
    const auto clip = workspace_.commandWriter().apply(
      grapple::project::CreateClipCommand{
        workspace_.commandWriter().nextNodeId("clip"),
        viewModel.value().timeline.layers.front().sourceNodeId,
        workspace_.commandWriter().nextEdgeId("contains_clip"),
        grapple::timeline::ClipPayload{
          grapple::timeline::ClipKind::Video,
          grapple::foundation::TimeRange{
            viewModel.value().timeline.duration,
            grapple::foundation::TimeSeconds{viewModel.value().timeline.duration.value + duration.value}
          },
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            duration
          },
          1.0,
          selectedAsset->assetId,
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
    log_->append(QString{"Added %1 to timeline"}.arg(qString(selectedAsset->name)));
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
    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Deleted clip at %1"}.arg(qString(deleted.value().snapshot.revision.value())));
  }

  void moveSelectedClip(grapple::foundation::TimeSeconds delta) {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Move Clip requires a selected clip."});
      return;
    }

    const auto snapshot = workspace_.project().snapshot();
    if (!snapshot) {
      appendError(snapshot.error());
      return;
    }

    const grapple::graph::GraphNode* node = snapshot.value().graph.findNode(selectedNodeId_.value());
    if (node == nullptr || node->kind != grapple::graph::NodeKind::Clip) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_clip", "Move Clip only applies to selected clips."});
      return;
    }

    const auto* payload = std::get_if<grapple::timeline::ClipPayload>(&node->payload);
    if (payload == nullptr) {
      appendError(grapple::foundation::Error{"desktop.clip_payload_invalid", "Selected clip node must carry a clip payload."});
      return;
    }

    grapple::timeline::ClipPayload updatedPayload = *payload;
    const grapple::foundation::TimeSeconds nextStart{updatedPayload.timelineRange.start.value + delta.value};
    const grapple::foundation::TimeSeconds nextEnd{updatedPayload.timelineRange.end.value + delta.value};
    if (nextStart.value < 0.0) {
      appendError(grapple::foundation::Error{"desktop.clip_move_before_zero", "Move Clip would place the clip before timeline start."});
      return;
    }
    updatedPayload.timelineRange = grapple::foundation::TimeRange{nextStart, nextEnd};

    const auto moved = workspace_.commandWriter().apply(
      grapple::project::UpdateClipCommand{selectedNodeId_.value(), updatedPayload},
      userSource()
    );
    if (!moved) {
      appendError(moved.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Moved clip at %1"}.arg(qString(moved.value().snapshot.revision.value())));
  }

  void addEffectToSelectedTarget(std::string intent) {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Add Effect requires a selected camera."});
      return;
    }

    const auto created = workspace_.steward().createCameraTransformEffect(
      selectedNodeId_.value(),
      std::move(intent),
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, timelineDuration_}
    );
    if (!created) {
      appendError(created.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Steward added effect at %1"}.arg(qString(created.value().snapshot.revision.value())));
  }

  void setSelectedTargetNumericEffectParam(const std::string& paramName, double value) {
    if (paramName.empty()) {
      appendError(grapple::foundation::Error{"desktop.effect_param_name_empty", "Effect parameter name must not be empty."});
      return;
    }
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Set Effect Param requires a selected timeline target."});
      return;
    }

    const auto snapshot = workspace_.project().snapshot();
    if (!snapshot) {
      appendError(snapshot.error());
      return;
    }

    const grapple::graph::GraphEdge* targetEdge = nullptr;
    for (const grapple::graph::GraphEdge& edge : snapshot.value().graph.edges()) {
      if (edge.enabled &&
          edge.kind == grapple::graph::EdgeKind::Targets &&
          edge.targetNodeId == selectedNodeId_.value()) {
        if (targetEdge != nullptr) {
          appendError(grapple::foundation::Error{"desktop.effect_target_ambiguous", "Selected target has multiple attached effects; select a single-effect target for this edit."});
          return;
        }
        targetEdge = &edge;
      }
    }
    if (targetEdge == nullptr) {
      appendError(grapple::foundation::Error{"desktop.effect_target_missing", "Selected target has no attached effect."});
      return;
    }

    setEffectNumericParam(targetEdge->sourceNodeId, paramName, value);
  }

  void deleteSelectedTargetEffect() {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Delete Effect requires a selected timeline target."});
      return;
    }

    const auto snapshot = workspace_.project().snapshot();
    if (!snapshot) {
      appendError(snapshot.error());
      return;
    }

    const grapple::graph::GraphEdge* targetEdge = nullptr;
    for (const grapple::graph::GraphEdge& edge : snapshot.value().graph.edges()) {
      if (edge.enabled &&
          edge.kind == grapple::graph::EdgeKind::Targets &&
          edge.targetNodeId == selectedNodeId_.value()) {
        if (targetEdge != nullptr) {
          appendError(grapple::foundation::Error{"desktop.effect_target_ambiguous", "Selected target has multiple attached effects; delete a specific effect from the parameters panel."});
          return;
        }
        targetEdge = &edge;
      }
    }
    if (targetEdge == nullptr) {
      appendError(grapple::foundation::Error{"desktop.effect_target_missing", "Selected target has no attached effect."});
      return;
    }

    deleteEffect(targetEdge->sourceNodeId);
  }

  void setEffectNumericParam(
    const grapple::foundation::NodeId& effectNodeId,
    const std::string& paramName,
    double value
  ) {
    const auto updated = workspace_.effects().setNumericParam(
      effectNodeId,
      paramName,
      value,
      userSource()
    );
    if (!updated) {
      appendError(updated.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Updated effect parameter at %1"}.arg(qString(updated.value().snapshot.revision.value())));
  }

  void deleteEffect(const grapple::foundation::NodeId& effectNodeId) {
    const auto deleted = workspace_.effects().deleteEffect(
      effectNodeId,
      userSource()
    );
    if (!deleted) {
      appendError(deleted.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Deleted effect at %1"}.arg(qString(deleted.value().snapshot.revision.value())));
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
    appendDiagnostics(result.value());
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

  void appendDiagnostics(const grapple::render::RenderFrameResult& result) {
    for (const grapple::runtime::RuntimeDiagnostic& diagnostic : result.runtimeDiagnostics) {
      log_->append(runtimeDiagnosticText(diagnostic));
    }
    for (const grapple::render::RenderDiagnostic& diagnostic : result.renderDiagnostics) {
      log_->append(renderDiagnosticText(diagnostic));
    }
  }

  void appendDiagnostics(const grapple::render::FinalRenderResult& result) {
    for (const grapple::runtime::RuntimeDiagnostic& diagnostic : result.runtimeDiagnostics) {
      log_->append(runtimeDiagnosticText(diagnostic));
    }
    for (const grapple::render::RenderDiagnostic& diagnostic : result.renderDiagnostics) {
      log_->append(renderDiagnosticText(diagnostic));
    }
  }

  void selectNode(grapple::foundation::NodeId nodeId) {
    selectedNodeId_ = std::move(nodeId);
    selectedAssetId_ = std::nullopt;
    mediaBin_->clearSelection();
    timeline_->setSelectedNodeId(selectedNodeId_);

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    updateInspector(viewModel.value());
  }

  void updateInspector(const grapple::app::AppViewModel& viewModel) {
    inspector_->setPlainText(inspectorText(viewModel, selectedNodeId_, selectedAssetId_));
    effectParams_->setSelection(viewModel, selectedNodeId_);
  }

  void rebuildMediaBin(const grapple::app::AppViewModel& viewModel) {
    mediaBin_->blockSignals(true);
    mediaBin_->clear();
    for (const grapple::app::AppAssetRow& asset : viewModel.assets.rows) {
      QStringList metadata;
      if (asset.duration.has_value()) {
        metadata << QString{"%1s"}.arg(asset.duration->value, 0, 'f', 2);
      }
      if (asset.dimensions.has_value()) {
        metadata << QString{"%1x%2"}.arg(asset.dimensions->width).arg(asset.dimensions->height);
      }
      const QString label = metadata.empty()
        ? QString{"%1 [%2]\n%3"}.arg(qString(asset.name)).arg(qString(asset.mediaType)).arg(qString(asset.assetId.value()))
        : QString{"%1 [%2]\n%3  %4"}.arg(qString(asset.name)).arg(qString(asset.mediaType)).arg(qString(asset.assetId.value())).arg(metadata.join("  "));
      auto* item = new QListWidgetItem{label};
      item->setData(Qt::UserRole, qString(asset.assetId.value()));
      mediaBin_->addItem(item);
      if (selectedAssetId_.has_value() && asset.assetId == selectedAssetId_.value()) {
        mediaBin_->setCurrentItem(item);
      }
    }
    mediaBin_->blockSignals(false);
  }

  void selectMediaAssetAtRow(int row) {
    QListWidgetItem* item = mediaBin_->item(row);
    if (item == nullptr) {
      return;
    }

    selectedAssetId_ = grapple::foundation::AssetId{item->data(Qt::UserRole).toString().toStdString()};
    selectedNodeId_ = std::nullopt;
    timeline_->setSelectedNodeId(selectedNodeId_);

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    updateInspector(viewModel.value());
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
  QListWidget* mediaBin_ = nullptr;
  QLabel* previewTitle_ = nullptr;
  QLabel* playheadLabel_ = nullptr;
  grapple::ui::PreviewSurface* previewSurface_ = nullptr;
  grapple::ui::TimelinePanel* timeline_ = nullptr;
  QTextEdit* inspector_ = nullptr;
  grapple::ui::EffectParamPanel* effectParams_ = nullptr;
  grapple::ui::StewardPanel* steward_ = nullptr;
  QTextEdit* log_ = nullptr;
  QFrame* previewFrame_ = nullptr;
  QTimer* playbackTimer_ = nullptr;
  grapple::foundation::TimeSeconds timelineDuration_;
  std::optional<grapple::foundation::NodeId> selectedNodeId_;
  std::optional<grapple::foundation::AssetId> selectedAssetId_;
};

} // namespace grapple::desktop

namespace grapple::desktop {

DesktopWindow::DesktopWindow(app::NativeWorkspaceSession& workspace)
  : impl_{std::make_unique<DesktopWindowImpl>(workspace)} {}

DesktopWindow::~DesktopWindow() = default;

void DesktopWindow::show() {
  impl_->show();
}

QPixmap DesktopWindow::grab() const {
  return impl_->grab();
}

void DesktopWindow::seekTo(foundation::TimeSeconds time) {
  impl_->seekTo(time);
}

void DesktopWindow::clickTimelineAtRatio(double ratio) {
  impl_->clickTimelineAtRatio(ratio);
}

void DesktopWindow::clickFirstTimelineClip() {
  impl_->clickFirstTimelineClip();
}

void DesktopWindow::clickFirstTimelineCamera() {
  impl_->clickFirstTimelineCamera();
}

std::optional<foundation::NodeId> DesktopWindow::selectedNodeId() const {
  return impl_->selectedNodeId();
}

std::optional<foundation::AssetId> DesktopWindow::selectedAssetId() const {
  return impl_->selectedAssetId();
}

std::string DesktopWindow::inspectorContents() const {
  return impl_->inspectorContents();
}

std::string DesktopWindow::logContents() const {
  return impl_->logContents();
}

std::string DesktopWindow::stewardContents() const {
  return impl_->stewardContents();
}

void DesktopWindow::setStewardIntent(std::string intent) {
  impl_->setStewardIntent(std::move(intent));
}

void DesktopWindow::clickStewardCreateCameraEffect() {
  impl_->clickStewardCreateCameraEffect();
}

void DesktopWindow::startPlayback() {
  impl_->startPlayback();
}

void DesktopWindow::pausePlayback() {
  impl_->pausePlayback();
}

void DesktopWindow::advancePlaybackFrame() {
  impl_->advancePlaybackFrame();
}

void DesktopWindow::addTrack() {
  impl_->addTrack();
}

void DesktopWindow::importVideoFile(const foundation::FilePath& path) {
  impl_->importVideoFile(path);
}

void DesktopWindow::addSelectedVideoToTimeline() {
  impl_->addSelectedVideoToTimeline();
}

void DesktopWindow::deleteSelectedClip() {
  impl_->deleteSelectedClip();
}

void DesktopWindow::moveSelectedClip(foundation::TimeSeconds delta) {
  impl_->moveSelectedClip(delta);
}

void DesktopWindow::setSelectedTargetNumericEffectParam(const std::string& paramName, double value) {
  impl_->setSelectedTargetNumericEffectParam(paramName, value);
}

void DesktopWindow::deleteSelectedTargetEffect() {
  impl_->deleteSelectedTargetEffect();
}

void DesktopWindow::openPackageRoot(const foundation::FilePath& rootPath) {
  impl_->openPackageRoot(rootPath);
}

} // namespace grapple::desktop
