#include "DesktopWindow.hpp"

#include <grapple/app/AppViewModel.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/media/MediaSource.hpp>
#include <grapple/render/RenderDiagnostic.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/ui_qt/CompositionViewport.hpp>
#include <grapple/ui_qt/EffectParamPanel.hpp>
#include <grapple/ui_qt/PreviewSurface.hpp>
#include <grapple/ui_qt/StewardPanel.hpp>
#include <grapple/ui_qt/TimelinePanel.hpp>

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
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
  QStringList lines{
    "Project",
    qString(viewModel.project.name),
    QString{"Duration: %1s"}.arg(viewModel.timeline.duration.value, 0, 'f', 2),
    QString{"Media: %1 assets, %2 clips"}.arg(viewModel.assets.count).arg(viewModel.timeline.clips.size()),
    QString{"Cameras: %1"}.arg(viewModel.timeline.cameras.size()),
    QString{"Editable effects: %1"}.arg(viewModel.timeline.effectCount),
    QString{"Notes: %1"}.arg(viewModel.notes.rows.size())
  };

  if (!viewModel.notes.rows.empty()) {
    lines << "";
    lines << "Notes";
    for (const grapple::app::AppNoteRow& note : viewModel.notes.rows) {
      lines << QString{"- %1"}.arg(qString(note.title));
    }
  }

  return lines.join('\n');
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
          qString(asset.name),
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
            QString paramText = QString{"%1=%2"}.arg(displayName).arg(qString(grapple::app::paramValueDisplayText(param.value)));
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
      return QString{"Effects\nNo effects attached."};
    }
    return QString{"Effects\n%1"}.arg(lines.join('\n'));
  };

  for (const grapple::app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nClip\nAsset: %1\nRange: %2s - %3s\n\n%4"}
        .arg(qString(clip.assetName))
        .arg(clip.timelineRange.start.value)
        .arg(clip.timelineRange.end.value)
        .arg(attachedEffectsText(clip.sourceNodeId));
    }
  }

  for (const grapple::app::AppCameraRow& camera : viewModel.timeline.cameras) {
    if (camera.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nCamera\nName: %1\n\n%2"}
        .arg(qString(camera.name))
        .arg(attachedEffectsText(camera.sourceNodeId));
    }
  }

  for (const grapple::app::AppLayerRow& layer : viewModel.timeline.layers) {
    if (layer.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nLayer\n%1\nClips: %2\n\n%3"}
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
    mediaBin_->setFlow(QListView::LeftToRight);
    mediaBin_->setWrapping(false);
    mediaBin_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mediaBin_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mediaBin_->setMaximumHeight(82);

    previewFrame_ = new QFrame;
    previewFrame_->setObjectName("previewFrame");
    previewFrame_->setMinimumSize(620, 420);
    auto* previewLayout = new QVBoxLayout{previewFrame_};
    previewTitle_ = new QLabel{"Preview - evaluated RenderPlan"};
    previewTitle_->setObjectName("panelTitle");
    previewSurface_ = new grapple::ui::PreviewSurface;
    previewLayout->addWidget(previewTitle_);
    previewLayout->addWidget(previewSurface_, 1);

    viewportFrame_ = new QFrame;
    viewportFrame_->setObjectName("viewportFrame");
    viewportFrame_->setMinimumSize(420, 420);
    auto* viewportLayout = new QVBoxLayout{viewportFrame_};
    viewportTitle_ = new QLabel{"Viewport - evaluated composition"};
    viewportTitle_->setObjectName("panelTitle");
    compositionViewport_ = new grapple::ui::CompositionViewport;
    viewportLayout->addWidget(viewportTitle_);
    viewportLayout->addWidget(compositionViewport_, 1);

    auto* studioPanel = new QWidget;
    studioPanel->setObjectName("studioPanel");
    auto* studioLayout = new QHBoxLayout{studioPanel};
    studioLayout->setContentsMargins(0, 0, 0, 0);
    studioLayout->setSpacing(12);
    studioLayout->addWidget(previewFrame_, 3);
    studioLayout->addWidget(viewportFrame_, 2);

    timeline_ = new grapple::ui::TimelinePanel;
    timeline_->setMinimumHeight(230);

    inspector_ = new QTextEdit;
    inspector_->setObjectName("inspector");
    inspector_->setReadOnly(true);

    effectParams_ = new grapple::ui::EffectParamPanel;
    effectParams_->setApplyHandler([this](
      grapple::foundation::NodeId effectNodeId,
      std::string paramName,
      grapple::timeline::ParamValue value
    ) {
      setEffectParamValue(effectNodeId, paramName, std::move(value));
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

    auto* refreshButton = new QPushButton{"Refresh"};
    playheadLabel_ = new QLabel;
    playheadLabel_->setObjectName("playheadLabel");
    auto* playButton = new QPushButton{"Play"};
    auto* pauseButton = new QPushButton{"Pause"};
    auto* seekStartButton = new QPushButton{"Start"};
    auto* stepBackButton = new QPushButton{"-1s"};
    auto* stepForwardButton = new QPushButton{"+1s"};
    auto* importVideoButton = new QPushButton{"Import"};
    auto* addMediaButton = new QPushButton{"Add To Timeline"};
    auto* undoButton = new QPushButton{"Undo"};
    auto* redoButton = new QPushButton{"Redo"};
    auto* exportButton = new QPushButton{"Export"};
    auto* saveButton = new QPushButton{"Save"};
    auto* moreButton = new QPushButton{"More"};
    auto* moreMenu = new QMenu{moreButton};
    auto* openPackageAction = moreMenu->addAction("Open Package");
    auto* addTrackAction = moreMenu->addAction("Add Track");
    auto* addCameraAction = moreMenu->addAction("Add Camera");
    auto* moveClipAction = moreMenu->addAction("Move Clip +1s");
    auto* deleteClipAction = moreMenu->addAction("Delete Clip");
    moreButton->setMenu(moreMenu);
    auto* productTitle = new QLabel{"Grapple"};
    productTitle->setObjectName("productTitle");
    auto* productSubtitle = new QLabel{"Prompt -> editable graph -> preview/export"};
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
    actionRow->addWidget(importVideoButton);
    actionRow->addWidget(addMediaButton);
    actionRow->addWidget(playheadLabel_);
    actionRow->addWidget(playButton);
    actionRow->addWidget(pauseButton);
    actionRow->addWidget(seekStartButton);
    actionRow->addWidget(stepBackButton);
    actionRow->addWidget(stepForwardButton);
    actionRow->addWidget(refreshButton);
    actionRow->addWidget(undoButton);
    actionRow->addWidget(redoButton);
    actionRow->addWidget(saveButton);
    actionRow->addWidget(exportButton);
    actionRow->addWidget(moreButton);

    auto* assetStrip = new QWidget;
    assetStrip->setObjectName("assetStrip");
    auto* assetStripLayout = new QHBoxLayout{assetStrip};
    assetStripLayout->setContentsMargins(10, 8, 10, 8);
    assetStripLayout->setSpacing(10);
    auto* assetTitle = new QLabel{"Assets"};
    assetTitle->setObjectName("panelTitle");
    assetStripLayout->addWidget(assetTitle);
    assetStripLayout->addWidget(mediaBin_, 1);

    auto* sidePanel = new QWidget;
    sidePanel->setObjectName("sidePanel");
    auto* sideLayout = new QVBoxLayout{sidePanel};
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(12);
    auto* detailsTabs = new QTabWidget;
    detailsTabs->setObjectName("detailsTabs");
    detailsTabs->addTab(inspector_, "Inspector");
    detailsTabs->addTab(summary_, "Project");
    detailsTabs->addTab(log_, "Log");
    sideLayout->addWidget(steward_, 3);
    sideLayout->addWidget(effectParams_, 2);
    sideLayout->addWidget(detailsTabs, 2);

    layout->addWidget(actions, 0, 0, 1, 2);
    layout->addWidget(studioPanel, 1, 0, 1, 1);
    layout->addWidget(sidePanel, 1, 1, 1, 1);
    layout->addWidget(assetStrip, 2, 0, 1, 2);
    layout->addWidget(timeline_, 3, 0, 1, 2);
    layout->setColumnStretch(0, 5);
    layout->setColumnStretch(1, 2);
    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 5);
    layout->setRowStretch(2, 0);
    layout->setRowStretch(3, 2);
    setCentralWidget(root);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshPreview(); });
    connect(playButton, &QPushButton::clicked, this, [this] { startPlayback(); });
    connect(pauseButton, &QPushButton::clicked, this, [this] { pausePlayback(); });
    connect(seekStartButton, &QPushButton::clicked, this, [this] { seekTo(grapple::foundation::TimeSeconds{0.0}); });
    connect(stepBackButton, &QPushButton::clicked, this, [this] { stepPlayhead(-1.0); });
    connect(stepForwardButton, &QPushButton::clicked, this, [this] { stepPlayhead(1.0); });
    connect(undoButton, &QPushButton::clicked, this, [this] { undoLastEdit(); });
    connect(redoButton, &QPushButton::clicked, this, [this] { redoLastEdit(); });
    connect(importVideoButton, &QPushButton::clicked, this, [this] { chooseAndImportVideo(); });
    connect(addMediaButton, &QPushButton::clicked, this, [this] { addSelectedVideoToTimeline(); });
    connect(openPackageAction, &QAction::triggered, this, [this] { chooseAndOpenPackage(); });
    connect(addTrackAction, &QAction::triggered, this, [this] { addTrack(); });
    connect(addCameraAction, &QAction::triggered, this, [this] { addCamera(); });
    connect(moveClipAction, &QAction::triggered, this, [this] { moveSelectedClip(grapple::foundation::TimeSeconds{1.0}); });
    connect(deleteClipAction, &QAction::triggered, this, [this] { deleteSelectedClip(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { chooseAndExportVideo(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { savePackage(); });
    steward_->setCreateCameraEffectHandler([this](std::string intent) { addEffectToSelectedTarget(std::move(intent)); });
    connect(mediaBin_, &QListWidget::currentRowChanged, this, [this](int row) { selectMediaAssetAtRow(row); });
    timeline_->setSeekHandler([this](grapple::foundation::TimeSeconds time) { seekTo(time); });
    timeline_->setSelectionHandler([this](grapple::foundation::NodeId nodeId) { selectNode(std::move(nodeId)); });

    setStyleSheet(R"(
      QMainWindow { background: #15171c; color: #e9edf5; }
      QWidget { background: #15171c; color: #e9edf5; font-family: "DejaVu Sans"; font-size: 14px; }
      QLabel#summary, QListWidget#mediaBin, QTextEdit#timeline, QTextEdit#inspector, QWidget#effectParams, QTextEdit#log, QWidget#actions, QWidget#assetStrip {
        background: #20242d; border: 1px solid #343b4a; border-radius: 10px; padding: 12px;
      }
      QTabWidget#detailsTabs::pane { background: #20242d; border: 1px solid #343b4a; border-radius: 10px; }
      QTabBar::tab {
        background: #151b25; color: #9fb0c8; border: 1px solid #343b4a; border-bottom: 0; border-top-left-radius: 8px; border-top-right-radius: 8px; padding: 7px 12px;
      }
      QTabBar::tab:selected { background: #20242d; color: #f2f7ff; }
      QWidget#sidePanel { background: transparent; }
      QWidget#stewardPanel, QTextEdit#stewardText, QTextEdit#stewardIntent {
        background: #20242d; border: 1px solid #343b4a; border-radius: 10px; color: #eaf3ff;
      }
      QTextEdit#stewardIntent { background: #10141d; padding: 8px 10px; }
      QListWidget#mediaBin { color: #dce8f6; outline: 0; }
      QListWidget#mediaBin::item { padding: 10px; border-radius: 8px; }
      QListWidget#mediaBin::item:selected { background: #36506f; color: #ffffff; }
      QTextEdit#inspector { color: #eaf3ff; }
      QTextEdit#log { color: #b8c7dc; }
      QFrame#previewFrame, QFrame#viewportFrame {
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
      QMenu {
        background: #20242d; color: #e9edf5; border: 1px solid #343b4a; border-radius: 8px; padding: 6px;
      }
      QMenu::item { padding: 7px 24px 7px 10px; border-radius: 6px; }
      QMenu::item:selected { background: #36506f; color: #ffffff; }
      QLineEdit, QDoubleSpinBox {
        background: #10141d; color: #e8f4ff; border: 1px solid #343b4a; border-radius: 8px; padding: 8px 10px;
      }
      QPushButton:hover { background: #79ddea; }
      QPushButton:disabled { background: #30404d; color: #9fb0c8; }
      QPushButton#effectParamDelete { background: #3a4658; color: #f4d4d4; }
      QPushButton#effectParamDelete:hover { background: #5a3841; }
    )");

    selectInitialCamera();
    refreshViewModel();
    refreshPreview();
  }

  void selectInitialCamera() {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.cameras.empty()) {
      return;
    }

    selectedNodeId_ = viewModel.value().timeline.cameras.front().sourceNodeId;
    selectedAssetId_ = std::nullopt;
  }

  void refreshViewModel() {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    summary_->setText(summaryText(viewModel.value()));
    rebuildMediaBin(viewModel.value());
    previewSurface_->setAssetLabels(viewModel.value().assets);
    steward_->setViewModel(viewModel.value(), workspace_.steward().conversationState(), selectedNodeId_);
    timeline_->setViewModel(viewModel.value());
    timeline_->setPlayhead(workspace_.preview().state().playhead);
    timeline_->setSelectedNodeId(selectedNodeId_);
    compositionViewport_->setViewModel(viewModel.value());
    compositionViewport_->setPlayhead(workspace_.preview().state().playhead);
    compositionViewport_->setSelectedNodeId(selectedNodeId_);
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
    log_->append("Preview refreshed");
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
    compositionViewport_->setFrame(frame.value().frame);
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
    clickTimelineCameraAtIndex(0);
  }

  void clickSecondTimelineCamera() {
    clickTimelineCameraAtIndex(1);
  }

  void clickTimelineCameraAtIndex(std::size_t cameraIndex) {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (cameraIndex >= viewModel.value().timeline.cameras.size()) {
      appendError(grapple::foundation::Error{"desktop.camera_index_missing", "Requested timeline camera does not exist."});
      return;
    }

    const int cameraRow = static_cast<int>(viewModel.value().timeline.layers.size());
    const std::size_t cameraCount = viewModel.value().timeline.cameras.size();
    const double rowTop = 34.0 + (static_cast<double>(cameraRow) * 44.0);
    const double availableHeight = 32.0;
    const double gap = 4.0;
    const double laneHeight = std::max(12.0, (availableHeight - (gap * static_cast<double>(cameraCount - 1))) / static_cast<double>(cameraCount));
    const double y = rowTop + 6.0 + (static_cast<double>(cameraIndex) * (laneHeight + gap)) + (laneHeight * 0.5);
    QMouseEvent event{
      QEvent::MouseButtonPress,
      QPointF{180.0, y},
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

  grapple::foundation::Result<grapple::foundation::NodeId> ensureComposition() {
    auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      return viewModel.error();
    }
    if (!viewModel.value().timeline.compositions.empty()) {
      return viewModel.value().timeline.compositions.front().sourceNodeId;
    }

    const grapple::foundation::NodeId compositionNodeId = workspace_.commandWriter().nextNodeId("composition");
    const auto composition = workspace_.commandWriter().apply(
      grapple::project::CreateCompositionCommand{
        compositionNodeId,
        "Main"
      },
      userSource()
    );
    if (!composition) {
      return composition.error();
    }
    return compositionNodeId;
  }

  void addTrack() {
    auto compositionNodeId = ensureComposition();
    if (!compositionNodeId) {
      appendError(compositionNodeId.error());
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const std::size_t trackNumber = viewModel.value().timeline.layers.size() + 1;
    const auto result = workspace_.commandWriter().apply(
      grapple::project::CreateTrackCommand{
        workspace_.commandWriter().nextNodeId("track"),
        compositionNodeId.value(),
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
    log_->append("Added track");
  }

  void addCamera() {
    auto compositionNodeId = ensureComposition();
    if (!compositionNodeId) {
      appendError(compositionNodeId.error());
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const std::size_t cameraNumber = viewModel.value().timeline.cameras.size() + 1;
    const grapple::foundation::NodeId cameraNodeId = workspace_.commandWriter().nextNodeId("camera");
    const auto result = workspace_.commandWriter().apply(
      grapple::project::CreateCameraCommand{
        cameraNodeId,
        compositionNodeId.value(),
        workspace_.commandWriter().nextEdgeId("contains_camera"),
        grapple::timeline::CameraPayload{
          "Camera " + std::to_string(cameraNumber),
          grapple::timeline::Transform{},
          grapple::timeline::CameraLens{35.0}
        }
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedNodeId_ = cameraNodeId;
    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append("Added camera");
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
      appendError(grapple::foundation::Error{"desktop.asset_selection_missing", "Add To Timeline requires a selected video asset."});
      return;
    }

    auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
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
      appendError(grapple::foundation::Error{"desktop.asset_not_video", "Add To Timeline requires a video asset."});
      return;
    }
    if (!selectedAsset->duration.has_value()) {
      appendError(grapple::foundation::Error{"desktop.asset_duration_missing", "Selected media asset requires a duration before it can be placed on the timeline."});
      return;
    }

    const grapple::foundation::AssetId assetId = selectedAsset->assetId;
    const std::string assetName = selectedAsset->name;
    const grapple::foundation::TimeSeconds duration = *selectedAsset->duration;

    auto compositionNodeId = ensureComposition();
    if (!compositionNodeId) {
      appendError(compositionNodeId.error());
      return;
    }
    viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    if (viewModel.value().timeline.layers.empty()) {
      const auto track = workspace_.commandWriter().apply(
        grapple::project::CreateTrackCommand{
          workspace_.commandWriter().nextNodeId("track"),
          compositionNodeId.value(),
          workspace_.commandWriter().nextEdgeId("contains_track"),
          "Video"
        },
        userSource()
      );
      if (!track) {
        appendError(track.error());
        return;
      }
      viewModel = workspace_.project().buildViewModel();
      if (!viewModel) {
        appendError(viewModel.error());
        return;
      }
    }

    if (viewModel.value().timeline.cameras.empty()) {
      const auto camera = workspace_.commandWriter().apply(
        grapple::project::CreateCameraCommand{
          workspace_.commandWriter().nextNodeId("camera"),
          compositionNodeId.value(),
          workspace_.commandWriter().nextEdgeId("contains_camera"),
          grapple::timeline::CameraPayload{
            "Camera",
            grapple::timeline::Transform{},
            grapple::timeline::CameraLens{35.0}
          }
        },
        userSource()
      );
      if (!camera) {
        appendError(camera.error());
        return;
      }
      viewModel = workspace_.project().buildViewModel();
      if (!viewModel) {
        appendError(viewModel.error());
        return;
      }
    }

    const grapple::foundation::NodeId clipNodeId = workspace_.commandWriter().nextNodeId("clip");
    const auto clip = workspace_.commandWriter().apply(
      grapple::project::CreateClipCommand{
        clipNodeId,
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
          assetId,
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

    selectedNodeId_ = clipNodeId;
    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Added %1 to timeline"}.arg(qString(assetName)));
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
    log_->append("Deleted clip");
  }

  void moveSelectedClip(grapple::foundation::TimeSeconds delta) {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Move Clip requires a selected clip."});
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    std::optional<grapple::foundation::TimeSeconds> currentStart;
    for (const grapple::app::AppClipRow& clip : viewModel.value().timeline.clips) {
      if (clip.sourceNodeId == selectedNodeId_.value()) {
        currentStart = clip.timelineRange.start;
        break;
      }
    }
    if (!currentStart.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_clip", "Move Clip only applies to selected clips."});
      return;
    }

    const auto moved = workspace_.commandWriter().apply(
      grapple::project::MoveClipCommand{
        selectedNodeId_.value(),
        grapple::foundation::TimeSeconds{currentStart->value + delta.value}
      },
      userSource()
    );
    if (!moved) {
      appendError(moved.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append("Moved clip");
  }

  void undoLastEdit() {
    const auto undone = workspace_.commandWriter().undoLastCommittedCommand(
      userSource(),
      std::optional<std::string>{"undo"}
    );
    if (!undone) {
      appendError(undone.error());
      return;
    }

    selectedNodeId_ = std::nullopt;
    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append("Undo complete");
  }

  void redoLastEdit() {
    const auto redone = workspace_.commandWriter().redoLastUndoneCommand(
      userSource(),
      std::optional<std::string>{"redo"}
    );
    if (!redone) {
      appendError(redone.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append("Redo complete");
  }

  void setEffectParamControlValue(const std::string& paramName, double value) {
    auto* editor = findChild<QDoubleSpinBox*>(QString{"effectParamEditor_%1"}.arg(qString(paramName)));
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.effect_param_control_missing", "Effect parameter control not found."});
      return;
    }

    editor->setValue(value);
    Q_EMIT editor->editingFinished();
    QApplication::processEvents();
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
      refreshViewModel();
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append("Steward applied camera edit");
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

    setEffectParamValue(targetEdge->sourceNodeId, paramName, grapple::timeline::ParamValue{value});
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

  void setEffectParamValue(
    const grapple::foundation::NodeId& effectNodeId,
    const std::string& paramName,
    grapple::timeline::ParamValue value
  ) {
    const auto updated = workspace_.effects().setParamValue(
      effectNodeId,
      paramName,
      std::move(value),
      userSource()
    );
    if (!updated) {
      appendError(updated.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append("Updated effect parameter");
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
    log_->append("Deleted effect");
  }

  void exportVideoFile(const grapple::foundation::FilePath& path) {
    const auto prepare = workspace_.exportSession().prepareFromProject();
    if (!prepare) {
      appendError(prepare.error());
      return;
    }
    const auto result = workspace_.exportSession().renderToVideo(grapple::render::ExportSettings{
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, timelineDuration_},
      grapple::foundation::FrameRate{30, 1},
      grapple::foundation::Resolution{1920, 1080},
      grapple::render::Codec{"mjpeg"},
      grapple::render::RenderQuality::Final,
      path
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
    const auto write = workspace_.writePackage();
    if (!write) {
      appendError(write.error());
      return;
    }
    log_->append(QString{"Package saved\n%1\n%2\n%3\n%4\n%5\n%6\n%7"}
      .arg(qString(write.value().project.snapshotPath.value))
      .arg(qString(write.value().project.manifestPath.value))
      .arg(qString(write.value().project.commandLogPath.value))
      .arg(qString(write.value().project.eventLogPath.value))
      .arg(qString(write.value().project.schemaMigrationLogPath.value))
      .arg(qString(write.value().agentRunsPath.value))
      .arg(qString(write.value().agentEventsPath.value)));
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
    compositionViewport_->setSelectedNodeId(selectedNodeId_);

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    updateSelectionPanels(viewModel.value());
  }

  void updateInspector(const grapple::app::AppViewModel& viewModel) {
    inspector_->setPlainText(inspectorText(viewModel, selectedNodeId_, selectedAssetId_));
    effectParams_->setSelection(viewModel, selectedNodeId_);
  }

  void updateSelectionPanels(const grapple::app::AppViewModel& viewModel) {
    updateInspector(viewModel);
    steward_->setViewModel(viewModel, workspace_.steward().conversationState(), selectedNodeId_);
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
        ? QString{"%1 [%2]"}.arg(qString(asset.name)).arg(qString(asset.mediaType))
        : QString{"%1 [%2]\n%3"}.arg(qString(asset.name)).arg(qString(asset.mediaType)).arg(metadata.join("  "));
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
    compositionViewport_->setSelectedNodeId(selectedNodeId_);

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    updateSelectionPanels(viewModel.value());
  }

  void chooseAndImportVideo() {
    const QString path = QFileDialog::getOpenFileName(this, "Import Video", QString{}, "Video Files (*.mov *.mp4 *.avi *.mkv)");
    if (path.isEmpty()) {
      return;
    }
    importVideoFile(grapple::foundation::FilePath{path.toStdString()});
  }

  void chooseAndExportVideo() {
    const QString path = QFileDialog::getSaveFileName(this, "Export Video", "grapple-export.avi", "AVI Video (*.avi)");
    if (path.isEmpty()) {
      return;
    }
    exportVideoFile(grapple::foundation::FilePath{path.toStdString()});
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
  QLabel* viewportTitle_ = nullptr;
  QLabel* playheadLabel_ = nullptr;
  grapple::ui::PreviewSurface* previewSurface_ = nullptr;
  grapple::ui::CompositionViewport* compositionViewport_ = nullptr;
  grapple::ui::TimelinePanel* timeline_ = nullptr;
  QTextEdit* inspector_ = nullptr;
  grapple::ui::EffectParamPanel* effectParams_ = nullptr;
  grapple::ui::StewardPanel* steward_ = nullptr;
  QTextEdit* log_ = nullptr;
  QFrame* previewFrame_ = nullptr;
  QFrame* viewportFrame_ = nullptr;
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

void DesktopWindow::clickSecondTimelineCamera() {
  impl_->clickSecondTimelineCamera();
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

void DesktopWindow::addCamera() {
  impl_->addCamera();
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

void DesktopWindow::undoLastEdit() {
  impl_->undoLastEdit();
}

void DesktopWindow::redoLastEdit() {
  impl_->redoLastEdit();
}

void DesktopWindow::exportVideoFile(const foundation::FilePath& path) {
  impl_->exportVideoFile(path);
}

void DesktopWindow::setEffectParamControlValue(const std::string& paramName, double value) {
  impl_->setEffectParamControlValue(paramName, value);
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
