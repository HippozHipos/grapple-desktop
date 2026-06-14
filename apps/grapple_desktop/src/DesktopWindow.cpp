#include "DesktopWindow.hpp"

#include <grapple/app/AppViewModel.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/jobs/MainThreadDispatcher.hpp>
#include <grapple/render/RenderDiagnostic.hpp>
#include <grapple/runtime/RuntimeDiagnostic.hpp>
#include <grapple/timeline/Payloads.hpp>
#include <grapple/ui_qt/CameraPropertyPanel.hpp>
#include <grapple/ui_qt/ClipTransformPanel.hpp>
#include <grapple/ui_qt/CompositionViewport.hpp>
#include <grapple/ui_qt/EffectParamPanel.hpp>
#include <grapple/ui_qt/ExportSettingsPanel.hpp>
#include <grapple/ui_qt/PreviewSurface.hpp>
#include <grapple/ui_qt/StewardPanel.hpp>
#include <grapple/ui_qt/TimelinePanel.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStringList>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace {

constexpr double DefaultImageClipDurationSeconds = 5.0;

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
    QString{"Media: %1 assets, %2 clips"}.arg(viewModel.assets.count).arg(viewModel.timeline.clips.size() + viewModel.timeline.audioClips.size()),
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

  auto selectedClipText = [&](const grapple::app::AppClipRow& clip) {
    return QString{"Inspector\nClip\nAsset: %1\nType: %2\nRange: %3s - %4s\nPosition: %5, %6\nScale: %7, %8\nOpacity: %9\n\n%10"}
      .arg(qString(clip.assetName))
      .arg(qString(clip.kind))
      .arg(clip.timelineRange.start.value)
      .arg(clip.timelineRange.end.value)
      .arg(clip.transform.position.x, 0, 'f', 2)
      .arg(clip.transform.position.y, 0, 'f', 2)
      .arg(clip.transform.scale.x, 0, 'f', 2)
      .arg(clip.transform.scale.y, 0, 'f', 2)
      .arg(clip.transform.opacity, 0, 'f', 2)
      .arg(attachedEffectsText(clip.sourceNodeId));
  };

  for (const grapple::app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.sourceNodeId == selectedNodeId.value()) {
      return selectedClipText(clip);
    }
  }

  for (const grapple::app::AppClipRow& clip : viewModel.timeline.audioClips) {
    if (clip.sourceNodeId == selectedNodeId.value()) {
      return selectedClipText(clip);
    }
  }

  for (const grapple::app::AppCameraRow& camera : viewModel.timeline.cameras) {
    if (camera.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nCamera\nName: %1\nFocal Length: %2\n\n%3"}
        .arg(qString(camera.name))
        .arg(camera.lens.focalLength, 0, 'f', 1)
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

  for (const grapple::app::AppLayerRow& track : viewModel.timeline.audioTracks) {
    if (track.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nAudio Track\n%1\nClips: %2\n\n%3"}
        .arg(qString(track.name))
        .arg(track.clipCount)
        .arg(attachedEffectsText(track.sourceNodeId));
    }
  }

  for (const grapple::app::AppNoteRow& note : viewModel.notes.rows) {
    if (note.sourceNodeId == selectedNodeId.value()) {
      return QString{"Inspector\nNote\n%1\n\n%2"}
        .arg(qString(note.title))
        .arg(qString(note.markdown));
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

grapple::foundation::Result<grapple::timeline::ClipKind> clipKindForMediaTypeName(
  const std::string& mediaType
) {
  if (mediaType == "video") {
    return grapple::timeline::ClipKind::Video;
  }
  if (mediaType == "audio") {
    return grapple::timeline::ClipKind::Audio;
  }
  if (mediaType == "image") {
    return grapple::timeline::ClipKind::Image;
  }
  return grapple::foundation::Error{"desktop.asset_media_type_unknown", "Selected media asset has an unknown media type."};
}

grapple::timeline::TrackKind trackKindForClipKind(grapple::timeline::ClipKind clipKind) {
  switch (clipKind) {
    case grapple::timeline::ClipKind::Video:
    case grapple::timeline::ClipKind::Image:
      return grapple::timeline::TrackKind::Visual;
    case grapple::timeline::ClipKind::Audio:
      return grapple::timeline::TrackKind::Audio;
  }

  std::abort();
}

grapple::foundation::Result<grapple::foundation::TimeSeconds> timelineDurationForAsset(
  const grapple::app::AppAssetRow& asset
) {
  if (asset.mediaType == "image") {
    return grapple::foundation::TimeSeconds{DefaultImageClipDurationSeconds};
  }
  if (asset.duration.has_value()) {
    return *asset.duration;
  }
  return grapple::foundation::Error{"desktop.asset_duration_missing", "Selected media asset requires a duration before it can be placed on the timeline."};
}

const grapple::app::AppClipRow* findClipRow(
  const grapple::app::AppViewModel& viewModel,
  const grapple::foundation::NodeId& nodeId
) {
  for (const grapple::app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.sourceNodeId == nodeId) {
      return &clip;
    }
  }
  for (const grapple::app::AppClipRow& clip : viewModel.timeline.audioClips) {
    if (clip.sourceNodeId == nodeId) {
      return &clip;
    }
  }
  return nullptr;
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

    cameraProperties_ = new grapple::ui::CameraPropertyPanel;
    cameraProperties_->setApplyHandler([this](
      grapple::foundation::NodeId cameraNodeId,
      std::string name,
      double focalLength
    ) {
      updateCameraProperties(cameraNodeId, std::move(name), focalLength);
    });

    clipTransform_ = new grapple::ui::ClipTransformPanel;
    clipTransform_->setApplyHandler([this](
      grapple::foundation::NodeId clipNodeId,
      grapple::foundation::Transform2D transform
    ) {
      updateClipTransform(clipNodeId, transform, "Updated clip transform");
    });

    effectParams_ = new grapple::ui::EffectParamPanel;
    effectParams_->setApplyHandler([this](
      grapple::foundation::NodeId effectNodeId,
      std::string paramName,
      grapple::timeline::ParamValue value
    ) {
      setEffectParamValue(effectNodeId, paramName, std::move(value));
    });
    effectParams_->setKeyframeHandler([this](
      grapple::foundation::NodeId effectNodeId,
      std::string paramName,
      grapple::timeline::ParamValue value,
      std::optional<grapple::foundation::KeyframeId> keyframeId
    ) {
      setEffectParamKeyframe(effectNodeId, paramName, std::move(value), std::move(keyframeId));
    });
    effectParams_->setDeleteKeyframeHandler([this](
      grapple::foundation::NodeId effectNodeId,
      std::string paramName,
      grapple::foundation::KeyframeId keyframeId
    ) {
      deleteEffectParamKeyframe(effectNodeId, paramName, keyframeId);
    });
    effectParams_->setDeleteHandler([this](grapple::foundation::NodeId effectNodeId) {
      deleteEffect(effectNodeId);
    });

    exportSettings_ = new grapple::ui::ExportSettingsPanel;
    exportSettingsDraft_ = exportSettings_->draft();
    exportSettings_->setApplyHandler([this](grapple::ui::ExportSettingsDraft draft) {
      exportSettingsDraft_ = std::move(draft);
    });

    log_ = new QTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);

    steward_ = new grapple::ui::StewardPanel;

    playbackTimer_ = new QTimer{this};
    playbackTimer_->setInterval(33);
    connect(playbackTimer_, &QTimer::timeout, this, [this] { advancePlaybackFrame(); });
    jobDispatchTimer_ = new QTimer{this};
    jobDispatchTimer_->setInterval(16);
    connect(jobDispatchTimer_, &QTimer::timeout, this, [this] { drainJobDispatch(); });
    jobDispatchTimer_->start();

    auto* refreshButton = new QPushButton{"Refresh"};
    playheadLabel_ = new QLabel;
    playheadLabel_->setObjectName("playheadLabel");
    auto* playButton = new QPushButton{"Play"};
    auto* pauseButton = new QPushButton{"Pause"};
    auto* seekStartButton = new QPushButton{"Start"};
    auto* stepBackButton = new QPushButton{"-1s"};
    auto* stepForwardButton = new QPushButton{"+1s"};
    auto* importMediaButton = new QPushButton{"Import"};
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
    auto* renameCameraAction = moreMenu->addAction("Rename Camera");
    auto* setCameraFocalLengthAction = moreMenu->addAction("Set Camera Focal Length");
    auto* addNoteAction = moreMenu->addAction("Add Note");
    auto* editNoteAction = moreMenu->addAction("Edit Note");
    auto* moveClipAction = moreMenu->addAction("Move Clip +1s");
    auto* trimClipAction = moreMenu->addAction("Trim Clip End -1s");
    auto* nudgeClipXAction = moreMenu->addAction("Nudge Clip X +0.1");
    auto* nudgeClipYAction = moreMenu->addAction("Nudge Clip Y +0.1");
    auto* scaleClipAction = moreMenu->addAction("Scale Clip 1.25x");
    auto* opacityClipAction = moreMenu->addAction("Set Clip Opacity 0.5");
    auto* deleteClipAction = moreMenu->addAction("Delete Clip");
    auto* deleteTrackAction = moreMenu->addAction("Delete Track");
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
    actionRow->addWidget(importMediaButton);
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
    sideLayout->addWidget(cameraProperties_, 2);
    sideLayout->addWidget(clipTransform_, 2);
    sideLayout->addWidget(effectParams_, 2);
    sideLayout->addWidget(exportSettings_, 2);
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
    connect(importMediaButton, &QPushButton::clicked, this, [this] { chooseAndImportMedia(); });
    connect(addMediaButton, &QPushButton::clicked, this, [this] { addSelectedMediaToTimeline(); });
    connect(openPackageAction, &QAction::triggered, this, [this] { chooseAndOpenPackage(); });
    connect(addTrackAction, &QAction::triggered, this, [this] { addTrack(); });
    connect(addCameraAction, &QAction::triggered, this, [this] { addCamera(); });
    connect(renameCameraAction, &QAction::triggered, this, [this] { renameSelectedCamera(); });
    connect(setCameraFocalLengthAction, &QAction::triggered, this, [this] { editSelectedCameraFocalLength(); });
    connect(addNoteAction, &QAction::triggered, this, [this] { addNote(); });
    connect(editNoteAction, &QAction::triggered, this, [this] { editSelectedNote(); });
    connect(moveClipAction, &QAction::triggered, this, [this] { moveSelectedClip(grapple::foundation::TimeSeconds{1.0}); });
    connect(trimClipAction, &QAction::triggered, this, [this] { trimSelectedClipEnd(grapple::foundation::TimeSeconds{-1.0}); });
    connect(nudgeClipXAction, &QAction::triggered, this, [this] { nudgeSelectedClipX(0.1); });
    connect(nudgeClipYAction, &QAction::triggered, this, [this] { nudgeSelectedClipY(0.1); });
    connect(scaleClipAction, &QAction::triggered, this, [this] { setSelectedClipUniformScale(1.25); });
    connect(opacityClipAction, &QAction::triggered, this, [this] { setSelectedClipOpacity(0.5); });
    connect(deleteClipAction, &QAction::triggered, this, [this] { deleteSelectedClip(); });
    connect(deleteTrackAction, &QAction::triggered, this, [this] { deleteSelectedTrack(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { chooseAndExportVideo(); });
    connect(saveButton, &QPushButton::clicked, this, [this] { savePackage(); });
    steward_->setCreateCameraEffectHandler([this](std::string intent) { addEffectToSelectedTarget(std::move(intent)); });
    connect(mediaBin_, &QListWidget::currentRowChanged, this, [this](int row) { selectMediaAssetAtRow(row); });
    timeline_->setSeekHandler([this](grapple::foundation::TimeSeconds time) { seekTo(time); });
    timeline_->setSelectionHandler([this](grapple::foundation::NodeId nodeId) { selectNode(std::move(nodeId)); });

    setStyleSheet(R"(
      QMainWindow { background: #15171c; color: #e9edf5; }
      QWidget { background: #15171c; color: #e9edf5; font-family: "DejaVu Sans"; font-size: 14px; }
      QLabel#summary, QListWidget#mediaBin, QTextEdit#timeline, QTextEdit#inspector, QWidget#cameraPropertyPanel, QWidget#clipTransformPanel, QWidget#effectParams, QWidget#exportSettingsPanel, QTextEdit#log, QWidget#actions, QWidget#assetStrip {
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
      QLabel#cameraPropertyTitle { color: #e8f4ff; font-weight: 800; }
      QLabel#cameraPropertyHelp { color: #9fb0c8; }
      QLabel#clipTransformTitle { color: #e8f4ff; font-weight: 800; }
      QLabel#clipTransformHelp { color: #9fb0c8; }
      QLabel#exportSettingsTitle { color: #e8f4ff; font-weight: 800; }
      QLabel#playheadLabel { color: #d8f3ff; font-weight: 800; padding: 0 8px; }
      QPushButton {
        background: #58c7d8; color: #071015; border: 0; border-radius: 8px; padding: 6px 10px; min-height: 24px; font-weight: 700;
      }
      QMenu {
        background: #20242d; color: #e9edf5; border: 1px solid #343b4a; border-radius: 8px; padding: 6px;
      }
      QMenu::item { padding: 7px 24px 7px 10px; border-radius: 6px; }
      QMenu::item:selected { background: #36506f; color: #ffffff; }
      QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {
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

  ~DesktopWindowImpl() override {
    workspace_.jobs().waitUntilIdle();
    drainJobDispatch();
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
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.clips.empty()) {
      appendError(grapple::foundation::Error{"desktop.clip_missing", "No visual timeline clip exists."});
      return;
    }

    const grapple::app::AppClipRow& clip = viewModel.value().timeline.clips.front();
    const auto layer = std::find_if(
      viewModel.value().timeline.layers.begin(),
      viewModel.value().timeline.layers.end(),
      [&](const grapple::app::AppLayerRow& row) {
        return row.sourceNodeId == clip.trackNodeId;
      }
    );
    if (layer == viewModel.value().timeline.layers.end()) {
      appendError(grapple::foundation::Error{"desktop.clip_track_missing", "Selected visual clip track does not exist."});
      return;
    }

    const int row = static_cast<int>(std::distance(viewModel.value().timeline.layers.begin(), layer));
    clickTimelineClipAt(clip, row, viewModel.value().timeline.duration);
  }

  void clickTimelineClipAt(
    const grapple::app::AppClipRow& clip,
    int row,
    grapple::foundation::TimeSeconds timelineDuration
  ) {
    const int left = 150;
    const int right = std::max(left + 1, timeline_->width() - 16);
    const double duration = std::max(0.001, timelineDuration.value);
    const double midpoint = (clip.timelineRange.start.value + clip.timelineRange.end.value) * 0.5;
    const double normalized = std::clamp(midpoint / duration, 0.0, 1.0);
    QMouseEvent event{
      QEvent::MouseButtonPress,
      QPointF{
        static_cast<double>(left) + (normalized * static_cast<double>(right - left)),
        34.0 + (static_cast<double>(row) * 44.0) + 22.0
      },
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier
    };
    QApplication::sendEvent(timeline_, &event);
  }

  void clickFirstTimelineAudioClip() {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.audioClips.empty()) {
      appendError(grapple::foundation::Error{"desktop.audio_clip_missing", "No audio timeline clip exists."});
      return;
    }

    const grapple::app::AppClipRow& clip = viewModel.value().timeline.audioClips.front();
    const auto track = std::find_if(
      viewModel.value().timeline.audioTracks.begin(),
      viewModel.value().timeline.audioTracks.end(),
      [&](const grapple::app::AppLayerRow& row) {
        return row.sourceNodeId == clip.trackNodeId;
      }
    );
    if (track == viewModel.value().timeline.audioTracks.end()) {
      appendError(grapple::foundation::Error{"desktop.audio_clip_track_missing", "Selected audio clip track does not exist."});
      return;
    }

    const int audioRow = static_cast<int>(
      viewModel.value().timeline.layers.size() +
      static_cast<std::size_t>(std::distance(viewModel.value().timeline.audioTracks.begin(), track))
    );
    clickTimelineClipAt(clip, audioRow, viewModel.value().timeline.duration);
  }

  void clickFirstTimelineTrack() {
    QMouseEvent event{
      QEvent::MouseButtonPress,
      QPointF{24.0, 56.0},
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier
    };
    QApplication::sendEvent(timeline_, &event);
  }

  void clickFirstTimelineAudioTrack() {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    if (viewModel.value().timeline.audioTracks.empty()) {
      appendError(grapple::foundation::Error{"desktop.audio_track_missing", "No audio timeline track exists."});
      return;
    }

    const int audioRow = static_cast<int>(viewModel.value().timeline.layers.size());
    QMouseEvent event{
      QEvent::MouseButtonPress,
      QPointF{24.0, 34.0 + (static_cast<double>(audioRow) * 44.0) + 22.0},
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

    const int cameraRow = static_cast<int>(
      viewModel.value().timeline.layers.size() + viewModel.value().timeline.audioTracks.size()
    );
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
        "Video " + std::to_string(trackNumber),
        grapple::timeline::TrackKind::Visual
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
          grapple::timeline::Transform2D{},
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

  void updateSelectedCameraName(std::string name) {
    auto selectedCamera = selectedCameraRowForAction("Rename Camera");
    if (!selectedCamera) {
      appendError(selectedCamera.error());
      return;
    }

    const auto result = workspace_.commandWriter().apply(
      grapple::project::UpdateCameraCommand{
        selectedCamera.value().sourceNodeId,
        grapple::timeline::CameraPayload{
          std::move(name),
          selectedCamera.value().transform,
          selectedCamera.value().lens
        }
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append("Renamed camera");
  }

  void updateSelectedCameraFocalLength(double focalLength) {
    auto selectedCamera = selectedCameraRowForAction("Set Camera Focal Length");
    if (!selectedCamera) {
      appendError(selectedCamera.error());
      return;
    }

    const auto result = workspace_.commandWriter().apply(
      grapple::project::UpdateCameraCommand{
        selectedCamera.value().sourceNodeId,
        grapple::timeline::CameraPayload{
          selectedCamera.value().name,
          selectedCamera.value().transform,
          grapple::timeline::CameraLens{focalLength}
        }
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    refreshPreview();
    log_->append("Updated camera focal length");
  }

  void updateCameraProperties(
    const grapple::foundation::NodeId& cameraNodeId,
    std::string name,
    double focalLength
  ) {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const auto selectedCamera = std::find_if(
      viewModel.value().timeline.cameras.begin(),
      viewModel.value().timeline.cameras.end(),
      [&](const grapple::app::AppCameraRow& camera) {
        return camera.sourceNodeId == cameraNodeId;
      }
    );
    if (selectedCamera == viewModel.value().timeline.cameras.end()) {
      appendError(grapple::foundation::Error{"desktop.camera_missing", "Camera property update requires an existing camera."});
      return;
    }

    const auto result = workspace_.commandWriter().apply(
      grapple::project::UpdateCameraCommand{
        cameraNodeId,
        grapple::timeline::CameraPayload{
          std::move(name),
          selectedCamera->transform,
          grapple::timeline::CameraLens{focalLength}
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
    log_->append("Updated camera properties");
  }

  void renameSelectedCamera() {
    auto selectedCamera = selectedCameraRowForAction("Rename Camera");
    if (!selectedCamera) {
      appendError(selectedCamera.error());
      return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(
      this,
      "Rename Camera",
      "Name",
      QLineEdit::Normal,
      qString(selectedCamera.value().name),
      &accepted
    );
    if (!accepted) {
      return;
    }

    updateSelectedCameraName(name.toStdString());
  }

  void editSelectedCameraFocalLength() {
    auto selectedCamera = selectedCameraRowForAction("Set Camera Focal Length");
    if (!selectedCamera) {
      appendError(selectedCamera.error());
      return;
    }

    bool accepted = false;
    const double focalLength = QInputDialog::getDouble(
      this,
      "Set Camera Focal Length",
      "Focal length",
      selectedCamera.value().lens.focalLength,
      1.0,
      1000.0,
      1,
      &accepted
    );
    if (!accepted) {
      return;
    }

    updateSelectedCameraFocalLength(focalLength);
  }

  grapple::foundation::Result<grapple::app::AppCameraRow> selectedCameraRowForAction(const std::string& actionName) {
    if (!selectedNodeId_.has_value()) {
      return grapple::foundation::Error{
        "desktop.selection_missing",
        actionName + " requires a selected camera."
      };
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      return viewModel.error();
    }

    const grapple::app::AppCameraRow* selectedCamera = selectedCameraRow(viewModel.value());
    if (selectedCamera == nullptr) {
      return grapple::foundation::Error{
        "desktop.selected_node_not_camera",
        actionName + " only applies to selected cameras."
      };
    }

    return *selectedCamera;
  }

  const grapple::app::AppCameraRow* selectedCameraRow(const grapple::app::AppViewModel& viewModel) const {
    if (!selectedNodeId_.has_value()) {
      return nullptr;
    }
    const auto selectedCamera = std::find_if(
      viewModel.timeline.cameras.begin(),
      viewModel.timeline.cameras.end(),
      [&](const grapple::app::AppCameraRow& camera) {
        return camera.sourceNodeId == selectedNodeId_.value();
      }
    );
    return selectedCamera == viewModel.timeline.cameras.end() ? nullptr : &*selectedCamera;
  }

  void addNote() {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const std::size_t noteNumber = viewModel.value().notes.rows.size() + 1;
    const grapple::foundation::NodeId noteNodeId = workspace_.commandWriter().nextNodeId("note");
    const auto result = workspace_.commandWriter().apply(
      grapple::project::CreateNoteCommand{
        noteNodeId,
        grapple::timeline::NotePayload{
          "Note " + std::to_string(noteNumber),
          "Project note"
        }
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedNodeId_ = noteNodeId;
    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    log_->append("Added note");
  }

  void updateSelectedNote(std::string title, std::string markdown) {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Edit Note requires a selected note."});
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const grapple::app::AppNoteRow* selectedNote = selectedNoteRow(viewModel.value());
    if (selectedNote == nullptr) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_note", "Edit Note only applies to selected notes."});
      return;
    }

    const auto result = workspace_.commandWriter().apply(
      grapple::project::UpdateNoteCommand{
        selectedNote->sourceNodeId,
        grapple::timeline::NotePayload{std::move(title), std::move(markdown)}
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedAssetId_ = std::nullopt;
    refreshViewModel();
    log_->append("Updated note");
  }

  void editSelectedNote() {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Edit Note requires a selected note."});
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const grapple::app::AppNoteRow* selectedNote = selectedNoteRow(viewModel.value());
    if (selectedNote == nullptr) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_note", "Edit Note only applies to selected notes."});
      return;
    }

    bool titleAccepted = false;
    const QString title = QInputDialog::getText(
      this,
      "Edit Note",
      "Title",
      QLineEdit::Normal,
      qString(selectedNote->title),
      &titleAccepted
    );
    if (!titleAccepted) {
      return;
    }

    bool markdownAccepted = false;
    const QString markdown = QInputDialog::getMultiLineText(
      this,
      "Edit Note",
      "Body",
      qString(selectedNote->markdown),
      &markdownAccepted
    );
    if (!markdownAccepted) {
      return;
    }

    updateSelectedNote(title.toStdString(), markdown.toStdString());
  }

  const grapple::app::AppNoteRow* selectedNoteRow(const grapple::app::AppViewModel& viewModel) const {
    if (!selectedNodeId_.has_value()) {
      return nullptr;
    }
    const auto selectedNote = std::find_if(
      viewModel.notes.rows.begin(),
      viewModel.notes.rows.end(),
      [&](const grapple::app::AppNoteRow& note) {
        return note.sourceNodeId == selectedNodeId_.value();
      }
    );
    return selectedNote == viewModel.notes.rows.end() ? nullptr : &*selectedNote;
  }

  void importMediaFile(const grapple::foundation::FilePath& path) {
    const auto imported = workspace_.importMediaFile(path);
    if (!imported) {
      appendError(imported.error());
      return;
    }

    selectedNodeId_ = std::nullopt;
    selectedAssetId_ = imported.value();
    refreshViewModel();
    refreshPreview();
    log_->append(QString{"Imported %1"}.arg(qString(std::filesystem::path{path.value}.stem().string())));
  }

  void addSelectedMediaToTimeline() {
    if (!selectedAssetId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.asset_selection_missing", "Add To Timeline requires a selected media asset."});
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
    auto clipKind = clipKindForMediaTypeName(selectedAsset->mediaType);
    if (!clipKind) {
      appendError(clipKind.error());
      return;
    }
    const grapple::timeline::TrackKind trackKind = trackKindForClipKind(clipKind.value());
    auto duration = timelineDurationForAsset(*selectedAsset);
    if (!duration) {
      appendError(duration.error());
      return;
    }

    const grapple::foundation::AssetId assetId = selectedAsset->assetId;
    const std::string assetName = selectedAsset->name;
    const grapple::foundation::TimeSeconds clipDuration = duration.value();

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

    const auto matchingTracks = [&]() -> const std::vector<grapple::app::AppLayerRow>& {
      return trackKind == grapple::timeline::TrackKind::Audio
        ? viewModel.value().timeline.audioTracks
        : viewModel.value().timeline.layers;
    };

    if (matchingTracks().empty()) {
      const std::string trackName = trackKind == grapple::timeline::TrackKind::Audio ? "Audio" : "Video";
      const auto track = workspace_.commandWriter().apply(
        grapple::project::CreateTrackCommand{
          workspace_.commandWriter().nextNodeId("track"),
          compositionNodeId.value(),
          workspace_.commandWriter().nextEdgeId("contains_track"),
          trackName,
          trackKind
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

    if (trackKind == grapple::timeline::TrackKind::Visual && viewModel.value().timeline.cameras.empty()) {
      const auto camera = workspace_.commandWriter().apply(
        grapple::project::CreateCameraCommand{
          workspace_.commandWriter().nextNodeId("camera"),
          compositionNodeId.value(),
          workspace_.commandWriter().nextEdgeId("contains_camera"),
          grapple::timeline::CameraPayload{
            "Camera",
            grapple::timeline::Transform2D{},
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

    const std::vector<grapple::app::AppLayerRow>& targetTracks = matchingTracks();
    if (targetTracks.empty()) {
      appendError(grapple::foundation::Error{"desktop.track_missing_after_create", "Add To Timeline could not find the target track after creating it."});
      return;
    }

    const grapple::foundation::NodeId clipNodeId = workspace_.commandWriter().nextNodeId("clip");
    const std::int64_t clipOrder = trackKind == grapple::timeline::TrackKind::Audio
      ? static_cast<std::int64_t>(viewModel.value().timeline.audioClips.size())
      : static_cast<std::int64_t>(viewModel.value().timeline.clips.size());
    const auto clip = workspace_.commandWriter().apply(
      grapple::project::CreateClipCommand{
        clipNodeId,
        targetTracks.front().sourceNodeId,
        workspace_.commandWriter().nextEdgeId("contains_clip"),
        grapple::timeline::ClipPayload{
          clipKind.value(),
          grapple::foundation::TimeRange{
            viewModel.value().timeline.duration,
            grapple::foundation::TimeSeconds{viewModel.value().timeline.duration.value + clipDuration.value}
          },
          grapple::foundation::TimeRange{
            grapple::foundation::TimeSeconds{0.0},
            clipDuration
          },
          1.0,
          assetId,
          grapple::timeline::Transform2D{}
        },
        clipOrder
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

    const grapple::app::AppClipRow* selectedClip = findClipRow(viewModel.value(), selectedNodeId_.value());
    if (selectedClip == nullptr) {
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

  void deleteSelectedTrack() {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Delete Track requires a selected track."});
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const auto selectedLayer = std::find_if(
      viewModel.value().timeline.layers.begin(),
      viewModel.value().timeline.layers.end(),
      [&](const grapple::app::AppLayerRow& layer) {
        return layer.sourceNodeId == selectedNodeId_.value();
      }
    );
    const auto selectedAudioTrack = std::find_if(
      viewModel.value().timeline.audioTracks.begin(),
      viewModel.value().timeline.audioTracks.end(),
      [&](const grapple::app::AppLayerRow& track) {
        return track.sourceNodeId == selectedNodeId_.value();
      }
    );
    if (selectedLayer == viewModel.value().timeline.layers.end() && selectedAudioTrack == viewModel.value().timeline.audioTracks.end()) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_track", "Delete Track only applies to selected tracks."});
      return;
    }

    const auto deleted = workspace_.commandWriter().apply(
      grapple::project::DeleteTrackCommand{selectedNodeId_.value()},
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
    log_->append("Deleted track");
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

    const grapple::app::AppClipRow* selectedClip = findClipRow(viewModel.value(), selectedNodeId_.value());
    if (selectedClip == nullptr) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_clip", "Move Clip only applies to selected clips."});
      return;
    }

    const auto moved = workspace_.commandWriter().apply(
      grapple::project::MoveClipCommand{
        selectedNodeId_.value(),
        grapple::foundation::TimeSeconds{selectedClip->timelineRange.start.value + delta.value}
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

  void trimSelectedClipEnd(grapple::foundation::TimeSeconds delta) {
    if (!selectedNodeId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.selection_missing", "Trim Clip requires a selected clip."});
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const grapple::app::AppClipRow* selectedClip = findClipRow(viewModel.value(), selectedNodeId_.value());
    if (selectedClip == nullptr) {
      appendError(grapple::foundation::Error{"desktop.selected_node_not_clip", "Trim Clip only applies to selected clips."});
      return;
    }

    const grapple::foundation::TimeSeconds timelineEnd{
      selectedClip->timelineRange.end.value + delta.value
    };
    if (timelineEnd.value <= selectedClip->timelineRange.start.value) {
      appendError(grapple::foundation::Error{"desktop.clip_trim_timeline_range_invalid", "Trim Clip End must leave the clip with positive duration."});
      return;
    }

    const double sourceDelta = delta.value * selectedClip->playbackRate;
    const grapple::foundation::TimeSeconds adjustedSourceEnd{
      selectedClip->sourceRange.end.value + sourceDelta
    };
    if (adjustedSourceEnd.value <= selectedClip->sourceRange.start.value) {
      appendError(grapple::foundation::Error{"desktop.clip_trim_source_range_invalid", "Trim Clip End must leave a positive source range."});
      return;
    }

    const auto trimmed = workspace_.commandWriter().apply(
      grapple::project::TrimClipCommand{
        selectedClip->sourceNodeId,
        grapple::foundation::TimeRange{selectedClip->timelineRange.start, timelineEnd},
        grapple::foundation::TimeRange{selectedClip->sourceRange.start, adjustedSourceEnd}
      },
      userSource()
    );
    if (!trimmed) {
      appendError(trimmed.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append("Trimmed clip");
  }

  void nudgeSelectedClipPosition(grapple::foundation::Vec2 delta) {
    auto selectedClip = selectedClipRowForAction("Nudge Clip");
    if (!selectedClip) {
      appendError(selectedClip.error());
      return;
    }

    grapple::timeline::Transform2D transform = selectedClip.value().transform;
    transform.position.x += delta.x;
    transform.position.y += delta.y;
    updateSelectedClipTransform(selectedClip.value(), transform, "Nudged clip");
  }

  void setSelectedClipUniformScale(double scale) {
    auto selectedClip = selectedClipRowForAction("Scale Clip");
    if (!selectedClip) {
      appendError(selectedClip.error());
      return;
    }

    grapple::timeline::Transform2D transform = selectedClip.value().transform;
    transform.scale = grapple::foundation::Vec2{scale, scale};
    updateSelectedClipTransform(selectedClip.value(), transform, "Scaled clip");
  }

  void setSelectedClipOpacity(double opacity) {
    auto selectedClip = selectedClipRowForAction("Set Clip Opacity");
    if (!selectedClip) {
      appendError(selectedClip.error());
      return;
    }

    grapple::timeline::Transform2D transform = selectedClip.value().transform;
    transform.opacity = opacity;
    updateSelectedClipTransform(selectedClip.value(), transform, "Updated clip opacity");
  }

  void updateSelectedClipTransform(
    const grapple::app::AppClipRow& selectedClip,
    grapple::timeline::Transform2D transform,
    const QString& logMessage
  ) {
    const auto updated = workspace_.commandWriter().apply(
      grapple::project::UpdateClipCommand{
        selectedClip.sourceNodeId,
        grapple::timeline::ClipPayload{
          selectedClip.clipKind,
          selectedClip.timelineRange,
          selectedClip.sourceRange,
          selectedClip.playbackRate,
          selectedClip.assetId,
          transform
        }
      },
      userSource()
    );
    if (!updated) {
      appendError(updated.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append(logMessage);
  }

  void updateClipTransform(
    const grapple::foundation::NodeId& clipNodeId,
    grapple::timeline::Transform2D transform,
    const QString& logMessage
  ) {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }

    const grapple::app::AppClipRow* selectedClip = findClipRow(viewModel.value(), clipNodeId);
    if (selectedClip == nullptr) {
      appendError(grapple::foundation::Error{"desktop.clip_missing", "Clip transform update requires an existing clip."});
      return;
    }

    selectedNodeId_ = clipNodeId;
    selectedAssetId_ = std::nullopt;
    updateSelectedClipTransform(*selectedClip, transform, logMessage);
  }

  grapple::foundation::Result<grapple::app::AppClipRow> selectedClipRowForAction(const std::string& actionName) {
    if (!selectedNodeId_.has_value()) {
      return grapple::foundation::Error{
        "desktop.selection_missing",
        actionName + " requires a selected clip."
      };
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      return viewModel.error();
    }

    const grapple::app::AppClipRow* selectedClip = findClipRow(viewModel.value(), selectedNodeId_.value());
    if (selectedClip == nullptr) {
      return grapple::foundation::Error{
        "desktop.selected_node_not_clip",
        actionName + " only applies to selected clips."
      };
    }

    return *selectedClip;
  }

  void nudgeSelectedClipX(double delta) {
    nudgeSelectedClipPosition(grapple::foundation::Vec2{delta, 0.0});
  }

  void nudgeSelectedClipY(double delta) {
    nudgeSelectedClipPosition(grapple::foundation::Vec2{0.0, delta});
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

  void setSelectedCameraNameControlValue(std::string name) {
    auto* editor = findChild<QLineEdit*>("cameraPropertyName");
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.camera_name_control_missing", "Camera name control not found."});
      return;
    }

    editor->setText(qString(name));
    Q_EMIT editor->editingFinished();
    QApplication::processEvents();
  }

  void setSelectedCameraFocalLengthControlValue(double focalLength) {
    auto* editor = findChild<QDoubleSpinBox*>("cameraPropertyFocalLength");
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.camera_focal_length_control_missing", "Camera focal length control not found."});
      return;
    }

    editor->setValue(focalLength);
    Q_EMIT editor->editingFinished();
    QApplication::processEvents();
  }

  void setSelectedClipTransformControlValue(std::string controlName, double value) {
    auto* editor = findChild<QDoubleSpinBox*>(qString(controlName));
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.clip_transform_control_missing", "Clip transform control not found."});
      return;
    }

    editor->setValue(value);
    Q_EMIT editor->editingFinished();
    QApplication::processEvents();
  }

  void setExportResolutionControlValue(int width, int height) {
    auto* widthEditor = findChild<QSpinBox*>("exportSettingsWidth");
    auto* heightEditor = findChild<QSpinBox*>("exportSettingsHeight");
    if (widthEditor == nullptr || heightEditor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.export_resolution_controls_missing", "Export resolution controls not found."});
      return;
    }

    widthEditor->setValue(width);
    Q_EMIT widthEditor->editingFinished();
    heightEditor->setValue(height);
    Q_EMIT heightEditor->editingFinished();
    QApplication::processEvents();
  }

  void setExportFrameRateControlValue(double framesPerSecond) {
    auto* editor = findChild<QDoubleSpinBox*>("exportSettingsFps");
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.export_fps_control_missing", "Export frame rate control not found."});
      return;
    }

    editor->setValue(framesPerSecond);
    Q_EMIT editor->editingFinished();
    QApplication::processEvents();
  }

  void setExportCodecControlValue(std::string codec) {
    auto* editor = findChild<QComboBox*>("exportSettingsCodec");
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.export_codec_control_missing", "Export codec control not found."});
      return;
    }

    const int index = editor->findText(qString(codec));
    if (index < 0) {
      appendError(grapple::foundation::Error{"desktop.export_codec_unknown", "Export codec control does not contain the requested codec."});
      return;
    }

    editor->setCurrentIndex(index);
    QApplication::processEvents();
  }

  void setEffectParamKeyframeAtPlayhead(const std::string& paramName) {
    auto* button = findChild<QPushButton*>(QString{"effectParamKeyframe_%1"}.arg(qString(paramName)));
    if (button == nullptr) {
      appendError(grapple::foundation::Error{"desktop.effect_keyframe_control_missing", "Effect keyframe control not found."});
      return;
    }

    button->click();
    QApplication::processEvents();
  }

  void deleteEffectParamKeyframeControl(const std::string& paramName, int keyframeIndex) {
    auto* button = findChild<QPushButton*>(QString{"effectParamDeleteKeyframe_%1_%2"}
      .arg(qString(paramName))
      .arg(keyframeIndex));
    if (button == nullptr) {
      appendError(grapple::foundation::Error{"desktop.effect_keyframe_delete_control_missing", "Effect keyframe delete control not found."});
      return;
    }

    button->click();
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

  void setEffectParamKeyframe(
    grapple::foundation::NodeId effectNodeId,
    std::string paramName,
    grapple::timeline::ParamValue value,
    std::optional<grapple::foundation::KeyframeId> keyframeId
  ) {
    const grapple::foundation::KeyframeId durableKeyframeId = keyframeId.has_value()
      ? keyframeId.value()
      : workspace_.commandWriter().nextKeyframeId(paramName);
    const auto updated = workspace_.effects().upsertParamKeyframe(
      std::move(effectNodeId),
      paramName,
      grapple::timeline::Param::Keyframe{
        durableKeyframeId,
        workspace_.preview().state().playhead,
        std::move(value)
      },
      userSource()
    );
    if (!updated) {
      appendError(updated.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append("Set effect keyframe");
  }

  void deleteEffectParamKeyframe(
    grapple::foundation::NodeId effectNodeId,
    std::string paramName,
    grapple::foundation::KeyframeId keyframeId
  ) {
    const auto deleted = workspace_.effects().deleteParamKeyframe(
      std::move(effectNodeId),
      std::move(paramName),
      std::move(keyframeId),
      userSource()
    );
    if (!deleted) {
      appendError(deleted.error());
      return;
    }

    refreshViewModel();
    refreshPreview();
    log_->append("Deleted effect keyframe");
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
    const bool started = startExportVideoFile(path);
    if (!started) {
      return;
    }
    waitForExportIdle();
  }

  bool startExportVideoFile(const grapple::foundation::FilePath& path) {
    if (exportInProgress_) {
      appendError(grapple::foundation::Error{"desktop.export_in_progress", "An export is already running."});
      return false;
    }

    auto plan = workspace_.project().buildRenderPlan();
    if (!plan) {
      appendError(plan.error());
      return false;
    }

    grapple::render::ExportSettings settings{
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, timelineDuration_},
      exportSettingsDraft_.frameRate,
      exportSettingsDraft_.resolution,
      exportSettingsDraft_.codec,
      grapple::render::RenderQuality::Final,
      path
    };

    const grapple::foundation::JobId jobId{
      "job_desktop_export_" + std::to_string(++exportJobCounter_)
    };
    exportInProgress_ = true;
    log_->append(QString{"Export queued -> %1"}.arg(qString(path.value)));
    auto enqueue = workspace_.jobs().enqueue(grapple::jobs::Job{
      jobId,
      "Desktop video export",
      [this, plan = std::move(plan.value().plan), settings](
        grapple::jobs::CancellationToken& cancellation,
        grapple::jobs::IProgressSink& progress
      ) mutable {
        if (cancellation.cancelled()) {
          return grapple::foundation::Result<void>{};
        }
        progress.reportProgress(0.0);
        auto result = workspace_.exportSession().renderPlanToVideo(std::move(plan), settings);
        progress.reportProgress(1.0);
        jobDispatcher_.post([this, result] {
          completeExport(result);
        });
        return result ? grapple::foundation::Result<void>{} : result.error();
      }
    });
    if (!enqueue) {
      exportInProgress_ = false;
      appendError(enqueue.error());
      return false;
    }
    return true;
  }

  void waitForExportIdle() {
    workspace_.jobs().waitUntilIdle();
    drainJobDispatch();
  }

  void drainJobDispatch() {
    jobDispatcher_.drain();
  }

  void completeExport(const grapple::foundation::Result<grapple::render::FinalRenderResult>& result) {
    exportInProgress_ = false;
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
    cameraProperties_->setSelection(viewModel, selectedNodeId_);
    clipTransform_->setSelection(viewModel, selectedNodeId_);
    effectParams_->setSelection(viewModel, selectedNodeId_, workspace_.preview().state().playhead);
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

  void chooseAndImportMedia() {
    const QString path = QFileDialog::getOpenFileName(
      this,
      "Import Media",
      QString{},
      "Media Files (*.mov *.mp4 *.avi *.mkv *.png *.jpg *.jpeg *.ppm *.webp *.wav *.aiff *.aif *.mp3 *.flac)"
    );
    if (path.isEmpty()) {
      return;
    }
    importMediaFile(grapple::foundation::FilePath{path.toStdString()});
  }

  void chooseAndExportVideo() {
    const QString path = QFileDialog::getSaveFileName(this, "Export Video", "grapple-export.avi", "AVI Video (*.avi)");
    if (path.isEmpty()) {
      return;
    }
    startExportVideoFile(grapple::foundation::FilePath{path.toStdString()});
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
  grapple::ui::CameraPropertyPanel* cameraProperties_ = nullptr;
  grapple::ui::ClipTransformPanel* clipTransform_ = nullptr;
  grapple::ui::EffectParamPanel* effectParams_ = nullptr;
  grapple::ui::ExportSettingsPanel* exportSettings_ = nullptr;
  grapple::ui::StewardPanel* steward_ = nullptr;
  QTextEdit* log_ = nullptr;
  QFrame* previewFrame_ = nullptr;
  QFrame* viewportFrame_ = nullptr;
  QTimer* playbackTimer_ = nullptr;
  QTimer* jobDispatchTimer_ = nullptr;
  grapple::jobs::MainThreadDispatcher jobDispatcher_;
  bool exportInProgress_ = false;
  int exportJobCounter_ = 0;
  grapple::foundation::TimeSeconds timelineDuration_;
  grapple::ui::ExportSettingsDraft exportSettingsDraft_;
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

void DesktopWindow::clickFirstTimelineTrack() {
  impl_->clickFirstTimelineTrack();
}

void DesktopWindow::clickFirstTimelineAudioTrack() {
  impl_->clickFirstTimelineAudioTrack();
}

void DesktopWindow::clickFirstTimelineClip() {
  impl_->clickFirstTimelineClip();
}

void DesktopWindow::clickFirstTimelineAudioClip() {
  impl_->clickFirstTimelineAudioClip();
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

void DesktopWindow::updateSelectedCameraName(std::string name) {
  impl_->updateSelectedCameraName(std::move(name));
}

void DesktopWindow::updateSelectedCameraFocalLength(double focalLength) {
  impl_->updateSelectedCameraFocalLength(focalLength);
}

void DesktopWindow::setSelectedCameraNameControlValue(std::string name) {
  impl_->setSelectedCameraNameControlValue(std::move(name));
}

void DesktopWindow::setSelectedCameraFocalLengthControlValue(double focalLength) {
  impl_->setSelectedCameraFocalLengthControlValue(focalLength);
}

void DesktopWindow::addNote() {
  impl_->addNote();
}

void DesktopWindow::updateSelectedNote(std::string title, std::string markdown) {
  impl_->updateSelectedNote(std::move(title), std::move(markdown));
}

void DesktopWindow::importMediaFile(const foundation::FilePath& path) {
  impl_->importMediaFile(path);
}

void DesktopWindow::addSelectedMediaToTimeline() {
  impl_->addSelectedMediaToTimeline();
}

void DesktopWindow::deleteSelectedClip() {
  impl_->deleteSelectedClip();
}

void DesktopWindow::deleteSelectedTrack() {
  impl_->deleteSelectedTrack();
}

void DesktopWindow::moveSelectedClip(foundation::TimeSeconds delta) {
  impl_->moveSelectedClip(delta);
}

void DesktopWindow::trimSelectedClipEnd(foundation::TimeSeconds delta) {
  impl_->trimSelectedClipEnd(delta);
}

void DesktopWindow::nudgeSelectedClipX(double delta) {
  impl_->nudgeSelectedClipX(delta);
}

void DesktopWindow::nudgeSelectedClipY(double delta) {
  impl_->nudgeSelectedClipY(delta);
}

void DesktopWindow::setSelectedClipUniformScale(double scale) {
  impl_->setSelectedClipUniformScale(scale);
}

void DesktopWindow::setSelectedClipOpacity(double opacity) {
  impl_->setSelectedClipOpacity(opacity);
}

void DesktopWindow::setSelectedClipTransformControlValue(std::string controlName, double value) {
  impl_->setSelectedClipTransformControlValue(std::move(controlName), value);
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

void DesktopWindow::setExportResolutionControlValue(int width, int height) {
  impl_->setExportResolutionControlValue(width, height);
}

void DesktopWindow::setExportFrameRateControlValue(double framesPerSecond) {
  impl_->setExportFrameRateControlValue(framesPerSecond);
}

void DesktopWindow::setExportCodecControlValue(std::string codec) {
  impl_->setExportCodecControlValue(std::move(codec));
}

void DesktopWindow::setEffectParamControlValue(const std::string& paramName, double value) {
  impl_->setEffectParamControlValue(paramName, value);
}

void DesktopWindow::setEffectParamKeyframeAtPlayhead(const std::string& paramName) {
  impl_->setEffectParamKeyframeAtPlayhead(paramName);
}

void DesktopWindow::deleteEffectParamKeyframeControl(const std::string& paramName, int keyframeIndex) {
  impl_->deleteEffectParamKeyframeControl(paramName, keyframeIndex);
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
