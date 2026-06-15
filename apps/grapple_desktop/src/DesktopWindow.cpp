#include "DesktopWindow.hpp"

#include <grapple/app/AppViewModel.hpp>
#include <grapple/app/NativeWorkspaceSession.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Hash.hpp>
#include <grapple/graph/GraphEdge.hpp>
#include <grapple/jobs/MainThreadDispatcher.hpp>
#include <grapple/project/ProjectMediaPlacement.hpp>
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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
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

QString renderProvenanceText(
  const grapple::render::RenderFrame& frame,
  const std::optional<grapple::foundation::RevisionId>& currentProjectRevision
) {
  if (currentProjectRevision.has_value() && frame.sourceRevision != currentProjectRevision.value()) {
    return QString{"stale preview: frame %1, project %2"}
      .arg(qString(frame.sourceRevision.value()))
      .arg(qString(currentProjectRevision->value()));
  }

  return QString{"current preview: %1"}
    .arg(qString(frame.sourceRevision.value()));
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

QString packageRootName(const grapple::foundation::FilePath& rootPath) {
  const std::filesystem::path path{rootPath.value};
  if (!path.filename().empty()) {
    return qString(path.filename().string());
  }
  return qString(rootPath.value);
}

QString inspectorText(
  const grapple::app::AppViewModel& viewModel,
  const std::optional<grapple::foundation::NodeId>& selectedNodeId,
  const std::optional<grapple::foundation::AssetId>& selectedAssetId,
  grapple::foundation::TimeSeconds playhead
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
        lines << QString{"Effect: %1"}.arg(qString(effect.displayName));
        if (effect.createdRevision.has_value()) {
          QString source = effect.createdActorName.empty()
            ? qString(effect.createdSourceKind)
            : qString(effect.createdActorName);
          QString provenance = QString{"Created by %1 at %2"}
            .arg(source)
            .arg(qString(effect.createdRevision->value()));
          if (!effect.createdIntent.empty()) {
            provenance += QString{": %1"}.arg(qString(effect.createdIntent));
          }
          lines << provenance;
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
            QString paramText = QString{"%1=%2"}
              .arg(displayName)
              .arg(qString(grapple::app::paramValueDisplayText(grapple::app::sampledEffectParamValue(param, playhead))));
            if (param.numericMin.has_value() && param.numericMax.has_value()) {
              paramText += QString{" [%1..%2"}.arg(*param.numericMin).arg(*param.numericMax);
              if (param.numericStep.has_value()) {
                paramText += QString{", step %1"}.arg(*param.numericStep);
              }
              paramText += ']';
            }
            if (param.lastEditedRevision.has_value()) {
              paramText += QString{" last changed by %1 at %2"}
                .arg(qString(param.lastEditedActorName.empty() ? param.lastEditedSourceKind : param.lastEditedActorName))
                .arg(qString(param.lastEditedRevision->value()));
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
        .arg(camera.state.lens.focalLength, 0, 'f', 1)
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

bool targetHasEditableEffects(
  const grapple::app::AppViewModel& viewModel,
  const grapple::foundation::NodeId& targetNodeId
) {
  for (const grapple::app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
    if (graph.targetNodeId != targetNodeId) {
      continue;
    }
    for (const grapple::app::AppEffectRow& effect : graph.effects) {
      if (!effect.params.empty()) {
        return true;
      }
    }
  }
  return false;
}

template <typename Widget>
Widget* uniqueEffectParamWidget(
  const QObject* root,
  const std::string& paramName,
  const QString& objectNamePrefix
) {
  Widget* match = nullptr;
  for (Widget* widget : root->findChildren<Widget*>()) {
    if (!widget->objectName().startsWith(objectNamePrefix) ||
        widget->property("effectParamName").toString() != qString(paramName)) {
      continue;
    }
    if (match != nullptr) {
      return nullptr;
    }
    match = widget;
  }
  return match;
}

QPushButton* uniqueEffectParamDeleteKeyframeButton(
  const QObject* root,
  const std::string& paramName,
  int keyframeIndex
) {
  QPushButton* match = nullptr;
  for (QPushButton* button : root->findChildren<QPushButton*>()) {
    if (!button->objectName().startsWith("effectParamDeleteKeyframe_") ||
        button->property("effectParamName").toString() != qString(paramName) ||
        button->property("effectParamKeyframeIndex").toInt() != keyframeIndex) {
      continue;
    }
    if (match != nullptr) {
      return nullptr;
    }
    match = button;
  }
  return match;
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
    previewFrame_->setMinimumSize(560, 420);
    auto* previewLayout = new QVBoxLayout{previewFrame_};
    previewTitle_ = new QLabel{"Player"};
    previewTitle_->setObjectName("panelTitle");
    previewSurface_ = new grapple::ui::PreviewSurface;
    previewLayout->addWidget(previewTitle_);
    previewLayout->addWidget(previewSurface_, 1);

    viewportFrame_ = new QFrame;
    viewportFrame_->setObjectName("viewportFrame");
    viewportFrame_->setMinimumSize(360, 420);
    auto* viewportLayout = new QVBoxLayout{viewportFrame_};
    viewportTitle_ = new QLabel{"Composition"};
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
    effectParamsScroll_ = new QScrollArea;
    effectParamsScroll_->setObjectName("effectParamsScroll");
    effectParamsScroll_->setWidgetResizable(true);
    effectParamsScroll_->setFrameShape(QFrame::NoFrame);
    effectParamsScroll_->setWidget(effectParams_);

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
    addSelectedMediaButton_ = new QPushButton{"Add To Timeline"};
    addSelectedMediaButton_->setEnabled(false);
    auto* undoButton = new QPushButton{"Undo"};
    auto* redoButton = new QPushButton{"Redo"};
    auto* exportButton = new QPushButton{"Export"};
    auto* saveButton = new QPushButton{"Save"};
    auto* moreButton = new QPushButton{"More"};
    auto* moreMenu = new QMenu{moreButton};
    auto* newPackageAction = moreMenu->addAction("New Package");
    auto* openPackageAction = moreMenu->addAction("Open Package");
    auto* saveAsPackageAction = moreMenu->addAction("Save As Package");
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
    productTitle_ = new QLabel{"Grapple"};
    productTitle_->setObjectName("productTitle");
    productSubtitle_ = new QLabel{"Prompt -> editable result -> preview/export"};
    productSubtitle_->setObjectName("productSubtitle");
    auto* titleBlock = new QWidget;
    auto* titleLayout = new QVBoxLayout{titleBlock};
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);
    titleLayout->addWidget(productTitle_);
    titleLayout->addWidget(productSubtitle_);

    auto* actions = new QWidget;
    actions->setObjectName("actions");
    auto* actionRow = new QHBoxLayout{actions};
    actionRow->setContentsMargins(10, 8, 10, 8);
    actionRow->setSpacing(8);
    actionRow->addWidget(titleBlock, 1);
    actionRow->addWidget(importMediaButton);
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
    assetStripLayout->addWidget(addSelectedMediaButton_);
    assetStripLayout->addWidget(mediaBin_, 1);

    auto* sidePanel = new QWidget;
    sidePanel->setObjectName("sidePanel");
    sidePanel->setMinimumWidth(400);
    auto* sideLayout = new QVBoxLayout{sidePanel};
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(12);
    detailTabs_ = new QTabWidget;
    detailTabs_->setObjectName("detailsTabs");
    detailTabs_->addTab(effectParamsScroll_, "Effects");
    detailTabs_->addTab(cameraProperties_, "Camera");
    detailTabs_->addTab(clipTransform_, "Clip");
    detailTabs_->addTab(exportSettings_, "Export");
    detailTabs_->addTab(inspector_, "Inspector");
    detailTabs_->addTab(summary_, "Project");
    detailTabs_->addTab(log_, "Log");
    sideLayout->addWidget(steward_, 0);
    sideLayout->addWidget(detailTabs_, 1);

    layout->addWidget(actions, 0, 0, 1, 2);
    layout->addWidget(studioPanel, 1, 0, 1, 1);
    layout->addWidget(sidePanel, 1, 1, 3, 1);
    layout->addWidget(assetStrip, 2, 0, 1, 1);
    layout->addWidget(timeline_, 3, 0, 1, 1);
    layout->setColumnStretch(0, 4);
    layout->setColumnStretch(1, 2);
    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 5);
    layout->setRowStretch(2, 0);
    layout->setRowStretch(3, 2);
    setCentralWidget(root);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshPreview(true); });
    connect(playButton, &QPushButton::clicked, this, [this] { startPlayback(); });
    connect(pauseButton, &QPushButton::clicked, this, [this] { pausePlayback(); });
    connect(seekStartButton, &QPushButton::clicked, this, [this] { seekTo(grapple::foundation::TimeSeconds{0.0}); });
    connect(stepBackButton, &QPushButton::clicked, this, [this] { stepPlayhead(-1.0); });
    connect(stepForwardButton, &QPushButton::clicked, this, [this] { stepPlayhead(1.0); });
    connect(undoButton, &QPushButton::clicked, this, [this] { undoLastEdit(); });
    connect(redoButton, &QPushButton::clicked, this, [this] { redoLastEdit(); });
    connect(importMediaButton, &QPushButton::clicked, this, [this] { chooseAndImportMedia(); });
    connect(addSelectedMediaButton_, &QPushButton::clicked, this, [this] { addSelectedMediaToTimeline(); });
    connect(newPackageAction, &QAction::triggered, this, [this] { chooseAndNewPackage(); });
    connect(openPackageAction, &QAction::triggered, this, [this] { chooseAndOpenPackage(); });
    connect(saveAsPackageAction, &QAction::triggered, this, [this] { chooseAndSavePackageAs(); });
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
    steward_->setImportMediaHandler([this] { chooseAndImportMedia(); });
    steward_->setAddCameraHandler([this] { addCamera(); });
    steward_->setAddSelectedMediaHandler([this] { placeSelectedMediaWithSteward(); });
    steward_->setShowCameraControlsHandler([this](grapple::foundation::NodeId cameraNodeId) {
      selectNode(std::move(cameraNodeId));
      showEffectControls();
    });
    steward_->setCreateCameraEffectHandler([this](std::string intent) { addEffectToSelectedTarget(std::move(intent)); });
    steward_->setAdjustCameraControlsHandler([this](
      grapple::foundation::NodeId cameraNodeId,
      std::string intent
    ) {
      adjustCameraControlsWithSteward(std::move(cameraNodeId), std::move(intent));
    });
    steward_->setTransformSelectedClipHandler([this](
      grapple::foundation::NodeId clipNodeId,
      std::string intent
    ) {
      transformSelectedClipWithSteward(std::move(clipNodeId), std::move(intent));
    });
    steward_->setSelectEditTargetHandler([this](grapple::foundation::NodeId targetNodeId) {
      selectNode(std::move(targetNodeId));
    });
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
      QWidget#stewardPanel, QTextEdit#stewardText, QTextEdit#stewardIntent, QListWidget#stewardRecentEdits {
        background: #20242d; border: 1px solid #343b4a; border-radius: 10px; color: #eaf3ff;
      }
      QScrollArea#effectParamsScroll { background: #20242d; border: 0; }
      QTextEdit#stewardIntent { background: #10141d; padding: 8px 10px; }
      QListWidget#mediaBin { color: #dce8f6; outline: 0; }
      QListWidget#mediaBin::item { padding: 10px; border-radius: 8px; }
      QListWidget#mediaBin::item:selected { background: #36506f; color: #ffffff; }
      QListWidget#stewardRecentEdits { outline: 0; padding: 6px; }
      QListWidget#stewardRecentEdits::item { padding: 7px 8px; border-radius: 7px; }
      QListWidget#stewardRecentEdits::item:selected { background: #36506f; color: #ffffff; }
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
    refreshViewModelAndPreview();
  }

  ~DesktopWindowImpl() override {
    workspace_.jobs().cancelAll();
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
    applyViewModel(viewModel.value());
  }

  void applyViewModel(const grapple::app::AppViewModel& viewModel) {
    currentViewModel_ = viewModel;
    currentProjectRevision_ = viewModel.project.revision;
    updateProjectHeader(viewModel);
    summary_->setText(summaryText(viewModel));
    rebuildMediaBin(viewModel);
    previewSurface_->setAssetLabels(viewModel.assets);
    steward_->setViewModel(viewModel, workspace_.steward().conversationState(), selectedNodeId_, selectedAssetId_);
    timeline_->setViewModel(viewModel);
    timeline_->setPlayhead(workspace_.preview().state().playhead);
    timeline_->setSelectedNodeId(selectedNodeId_);
    compositionViewport_->setViewModel(viewModel);
    compositionViewport_->setPlayhead(workspace_.preview().state().playhead);
    compositionViewport_->setSelectedNodeId(selectedNodeId_);
    updateActionAvailability();
    updateInspector(viewModel);
    timelineDuration_ = viewModel.timeline.duration;
  }

  void refreshViewModelAndPreview(bool logRefresh = false) {
    auto result = workspace_.project().buildViewModelAndRenderPlan();
    if (!result) {
      appendError(result.error());
      return;
    }
    applyViewModel(result.value().viewModel);
    refreshPreviewFromRenderPlan(result.value().renderPlan, logRefresh);
  }

  void refreshPreview(bool logRefresh = false) {
    const auto refresh = workspace_.preview().refreshFromProject();
    if (!refresh) {
      appendError(refresh.error());
      return;
    }
    renderCurrentFrame(true);
    if (logRefresh) {
      log_->append("Preview refreshed");
    }
  }

  void refreshPreviewFromRenderPlan(const grapple::projection::RenderPlan& plan, bool logRefresh = false) {
    const auto refresh = workspace_.preview().refreshFromRenderPlan(plan);
    if (!refresh) {
      appendError(refresh.error());
      return;
    }
    renderCurrentFrame(true);
    if (logRefresh) {
      log_->append("Preview refreshed");
    }
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
    auto renderedFrame = std::make_shared<const grapple::render::RenderFrame>(std::move(frame.value().frame));
    previewSurface_->setFrame(renderedFrame);
    compositionViewport_->setFrame(renderedFrame);
    const QString provenance = renderProvenanceText(*renderedFrame, currentProjectRevision_);
    previewTitle_->setText(QString{"Player  %1"}.arg(provenance));
    viewportTitle_->setText(QString{"Composition  %1"}.arg(provenance));
    playheadLabel_->setText(QString{"Playhead: %1"}.arg(timeText(renderedFrame->time)));
    timeline_->setPlayhead(renderedFrame->time);
    refreshPlaybackEditControlsIfNeeded();
  }

  void seekTo(grapple::foundation::TimeSeconds time, bool updateEditControls = true) {
    const auto seek = workspace_.preview().seek(time);
    if (!seek) {
      appendError(seek.error());
      return;
    }
    renderCurrentFrame();
    if (updateEditControls) {
      refreshPlayheadEditControls();
    }
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

  std::string projectHeaderText() const {
    return (productTitle_->text() + "\n" + productSubtitle_->text()).toStdString();
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

  std::string stewardPrimaryActionText() const {
    return steward_->primaryActionText();
  }

  bool stewardPrimaryActionEnabled() const {
    return steward_->primaryActionEnabled();
  }

  std::string stewardSelectedClipActionText() const {
    return steward_->selectedClipActionText();
  }

  bool stewardSelectedClipActionEnabled() const {
    return steward_->selectedClipActionEnabled();
  }

  bool addSelectedMediaActionEnabled() const {
    return addSelectedMediaButton_->isEnabled();
  }

  int stewardRecentEditCount() const {
    return steward_->recentEditCount();
  }

  int stewardCurrentRecentEditRow() const {
    return steward_->currentRecentEditRow();
  }

  std::string stewardRecentEditText(int row) const {
    return steward_->recentEditText(row);
  }

  std::string effectParamTitleText() const {
    auto* title = findChild<QLabel*>("effectParamTitle");
    if (title == nullptr) {
      return {};
    }
    return title->text().toStdString();
  }

  std::string effectParamPanelText() const {
    QStringList lines;
    for (QLabel* label : effectParams_->findChildren<QLabel*>()) {
      lines << label->text();
    }
    return lines.join('\n').toStdString();
  }

  bool effectParamControlVisible(const std::string& paramName) const {
    auto* editor = uniqueEffectParamWidget<QDoubleSpinBox>(this, paramName, "effectParamEditor_");
    if (editor == nullptr || effectParamsScroll_ == nullptr || effectParamsScroll_->viewport() == nullptr) {
      return false;
    }

    const QRect editorRect = editor->rect();
    const QPoint topLeft = editor->mapTo(effectParamsScroll_->viewport(), editorRect.topLeft());
    const QPoint bottomRight = editor->mapTo(effectParamsScroll_->viewport(), editorRect.bottomRight());
    const QRect viewportRect = effectParamsScroll_->viewport()->rect();
    return viewportRect.contains(topLeft) && viewportRect.contains(bottomRight);
  }

  std::optional<double> effectParamControlValue(const std::string& paramName) const {
    auto* editor = uniqueEffectParamWidget<QDoubleSpinBox>(this, paramName, "effectParamEditor_");
    if (editor == nullptr) {
      return std::nullopt;
    }
    return editor->value();
  }

  std::string currentDetailTabText() const {
    if (detailTabs_ == nullptr || detailTabs_->currentIndex() < 0) {
      return {};
    }
    return detailTabs_->tabText(detailTabs_->currentIndex()).toStdString();
  }

  std::string stewardIntent() const {
    return steward_->intent();
  }

  void setStewardIntent(std::string intent) {
    steward_->setIntent(std::move(intent));
  }

  void pressStewardSubmitShortcut() {
    auto* intentEditor = findChild<QTextEdit*>("stewardIntent");
    if (intentEditor == nullptr) {
      appendError(grapple::foundation::Error{"desktop.steward_intent_missing", "Steward intent editor not found."});
      return;
    }

    intentEditor->setFocus(Qt::OtherFocusReason);
    QKeyEvent press{
      QEvent::KeyPress,
      Qt::Key_Return,
      Qt::ControlModifier
    };
    QApplication::sendEvent(intentEditor, &press);
    QKeyEvent release{
      QEvent::KeyRelease,
      Qt::Key_Return,
      Qt::ControlModifier
    };
    QApplication::sendEvent(intentEditor, &release);
    QApplication::processEvents();
  }

  void clickStewardPrimaryAction() {
    steward_->triggerPrimaryAction();
  }

  void clickStewardSelectedClipAction() {
    steward_->triggerSelectedClipAction();
  }

  void clickStewardRecentEdit(int row) {
    steward_->triggerRecentEdit(row);
  }

  void showEffectControls() {
    detailTabs_->setCurrentWidget(effectParamsScroll_);
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
    refreshPlayheadEditControls();
  }

  void advancePlaybackFrame() {
    if (workspace_.preview().state().playback != grapple::render::PreviewPlaybackState::Playing) {
      playbackTimer_->stop();
      return;
    }

    const double duration = std::max(0.0, timelineDuration_.value);
    const double next = workspace_.preview().state().playhead.value + (1.0 / 30.0);
    if (duration <= 0.0 || next >= duration) {
      seekTo(grapple::foundation::TimeSeconds{duration}, false);
      pausePlayback();
      return;
    }

    seekTo(grapple::foundation::TimeSeconds{next}, false);
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

    refreshViewModelAndPreview();
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
          grapple::timeline::CameraState{
            grapple::timeline::Transform2D{},
            grapple::timeline::CameraLens{35.0}
          }
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
    refreshViewModelAndPreview();
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
          selectedCamera.value().state
        }
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedAssetId_ = std::nullopt;
    refreshViewModelAndPreview();
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
          grapple::timeline::CameraState{
            selectedCamera.value().state.transform,
            grapple::timeline::CameraLens{focalLength}
          }
        }
      },
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedAssetId_ = std::nullopt;
    refreshViewModelAndPreview();
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
          grapple::timeline::CameraState{
            selectedCamera->state.transform,
            grapple::timeline::CameraLens{focalLength}
          }
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
    refreshViewModelAndPreview();
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
      selectedCamera.value().state.lens.focalLength,
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

  grapple::foundation::Result<grapple::foundation::NodeId> stewardCameraTargetForAction() const {
    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      return viewModel.error();
    }

    const std::optional<grapple::foundation::NodeId> targetCameraNodeId =
      grapple::app::stewardCameraTargetId(viewModel.value(), selectedNodeId_);
    if (targetCameraNodeId.has_value()) {
      return targetCameraNodeId.value();
    }
    if (viewModel.value().timeline.cameras.empty()) {
      return grapple::foundation::Error{"desktop.camera_missing", "Add Effect requires a camera."};
    }
    return grapple::foundation::Error{"desktop.selection_missing", "Add Effect requires a selected camera."};
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
    refreshViewModelAndPreview();
    log_->append(QString{"Imported %1"}.arg(qString(std::filesystem::path{path.value}.stem().string())));
  }

  void addSelectedMediaToTimeline() {
    if (!selectedAssetId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.asset_selection_missing", "Adding media to the timeline requires a selected media asset."});
      return;
    }

    auto snapshot = workspace_.project().snapshot();
    if (!snapshot) {
      appendError(snapshot.error());
      return;
    }

    const grapple::asset::Asset* selectedAsset = snapshot.value().assets.find(selectedAssetId_.value());
    if (selectedAsset == nullptr) {
      appendError(grapple::foundation::Error{"desktop.asset_missing", "Selected media asset does not exist in the project."});
      return;
    }
    auto compositions = grapple::project::inspectCompositions(snapshot.value());
    if (!compositions) {
      appendError(compositions.error());
      return;
    }
    auto placement = grapple::project::buildMediaPlacementDraft(
      workspace_.commandWriter(),
      *selectedAsset,
      std::nullopt,
      std::nullopt,
      compositions.value().compositions
    );
    if (!placement) {
      appendError(placement.error());
      return;
    }

    const auto result = workspace_.commandWriter().apply(
      std::move(placement.value().command),
      userSource()
    );
    if (!result) {
      appendError(result.error());
      return;
    }

    selectedNodeId_ = placement.value().clipNodeId;
    selectedAssetId_ = std::nullopt;
    refreshViewModelAndPreview();
    log_->append("Added selected media to timeline");
  }

  void placeSelectedMediaWithSteward() {
    if (!selectedAssetId_.has_value()) {
      appendError(grapple::foundation::Error{"desktop.asset_selection_missing", "Adding media to the timeline requires a selected media asset."});
      return;
    }

    const auto mediaPlacement = workspace_.steward().placeAssetOnTimeline(selectedAssetId_.value());
    if (!mediaPlacement) {
      appendError(mediaPlacement.error());
      return;
    }

    selectedNodeId_ = mediaPlacement.value().clipNodeId;
    selectedAssetId_ = std::nullopt;
    refreshViewModelAndPreview();
    log_->append("Steward added selected media to timeline");
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
    refreshViewModelAndPreview();
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
    refreshViewModelAndPreview();
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

    refreshViewModelAndPreview();
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

    refreshViewModelAndPreview();
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

    refreshViewModelAndPreview();
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
    refreshViewModelAndPreview();
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

    refreshViewModelAndPreview();
    log_->append("Redo complete");
  }

  void setEffectParamControlValue(const std::string& paramName, double value) {
    auto* editor = uniqueEffectParamWidget<QDoubleSpinBox>(this, paramName, "effectParamEditor_");
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{
        "desktop.effect_param_control_missing",
        "Effect parameter control not found or ambiguous."
      });
      return;
    }

    editor->setValue(value);
    Q_EMIT editor->editingFinished();
    QApplication::processEvents();
  }

  void setEffectParamControlDraftValue(const std::string& paramName, double value) {
    auto* editor = uniqueEffectParamWidget<QDoubleSpinBox>(this, paramName, "effectParamEditor_");
    if (editor == nullptr) {
      appendError(grapple::foundation::Error{
        "desktop.effect_param_control_missing",
        "Effect parameter control not found or ambiguous."
      });
      return;
    }

    editor->setValue(value);
    QApplication::processEvents();
  }

  void setEffectParamSliderRatio(const std::string& paramName, double ratio) {
    auto* slider = uniqueEffectParamWidget<QSlider>(this, paramName, "effectParamSlider_");
    if (slider == nullptr) {
      appendError(grapple::foundation::Error{
        "desktop.effect_param_slider_missing",
        "Effect parameter slider not found or ambiguous."
      });
      return;
    }

    const double normalized = std::clamp(ratio, 0.0, 1.0);
    const int value = slider->minimum() +
                      static_cast<int>(std::lround(normalized * static_cast<double>(slider->maximum() - slider->minimum())));
    slider->setValue(value);
    Q_EMIT slider->sliderReleased();
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
    auto* button = uniqueEffectParamWidget<QPushButton>(this, paramName, "effectParamKeyframe_");
    if (button == nullptr) {
      appendError(grapple::foundation::Error{
        "desktop.effect_keyframe_control_missing",
        "Effect keyframe control not found or ambiguous."
      });
      return;
    }

    button->click();
    QApplication::processEvents();
  }

  std::string effectParamKeyframeButtonText(const std::string& paramName) const {
    auto* button = uniqueEffectParamWidget<QPushButton>(this, paramName, "effectParamKeyframe_");
    if (button == nullptr) {
      return {};
    }
    return button->text().toStdString();
  }

  void deleteEffectParamKeyframeControl(const std::string& paramName, int keyframeIndex) {
    auto* button = uniqueEffectParamDeleteKeyframeButton(this, paramName, keyframeIndex);
    if (button == nullptr) {
      appendError(grapple::foundation::Error{
        "desktop.effect_keyframe_delete_control_missing",
        "Effect keyframe delete control not found or ambiguous."
      });
      return;
    }

    button->click();
    QApplication::processEvents();
  }

  void addEffectToSelectedTarget(std::string intent) {
    const auto targetCameraNodeId = stewardCameraTargetForAction();
    if (!targetCameraNodeId) {
      appendError(targetCameraNodeId.error());
      return;
    }

    const auto created = workspace_.steward().createCameraTransformEffect(
      targetCameraNodeId.value(),
      std::move(intent),
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, timelineDuration_}
    );
    if (!created) {
      appendError(created.error());
      refreshViewModel();
      return;
    }

    selectedNodeId_ = targetCameraNodeId.value();
    selectedAssetId_ = std::nullopt;
    showEffectControls();
    refreshViewModelAndPreview();
    steward_->setIntent({});
    log_->append("Steward applied camera edit");
  }

  void transformSelectedClipWithSteward(
    grapple::foundation::NodeId clipNodeId,
    std::string intent
  ) {
    const auto transformed = workspace_.steward().transformClip(clipNodeId, std::move(intent));
    if (!transformed) {
      appendError(transformed.error());
      refreshViewModel();
      return;
    }

    selectedNodeId_ = clipNodeId;
    selectedAssetId_ = std::nullopt;
    refreshViewModelAndPreview();
    steward_->setIntent({});
    log_->append("Steward transformed selected clip");
  }

  void adjustCameraControlsWithSteward(
    grapple::foundation::NodeId cameraNodeId,
    std::string intent
  ) {
    const auto adjusted = workspace_.steward().adjustCameraTransformControls(
      cameraNodeId,
      std::move(intent),
      grapple::foundation::TimeRange{grapple::foundation::TimeSeconds{0.0}, timelineDuration_}
    );
    if (!adjusted) {
      appendError(adjusted.error());
      refreshViewModel();
      return;
    }

    selectedNodeId_ = cameraNodeId;
    selectedAssetId_ = std::nullopt;
    showEffectControls();
    refreshViewModelAndPreview();
    steward_->setIntent({});
    log_->append("Steward adjusted camera controls");
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
    if (!updated.value().changed) {
      return;
    }

    refreshViewModelAndPreview();
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
    if (!updated.value().changed) {
      return;
    }

    refreshViewModelAndPreview();
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

    refreshViewModelAndPreview();
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

    refreshViewModelAndPreview();
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
    activeExportJobId_ = jobId;
    lastLoggedExportProgressPercent_ = -25;
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
        auto result = workspace_.exportSession().renderPlanToVideo(plan, settings, &progress, &cancellation);
        jobDispatcher_.post([this, result] {
          completeExport(result);
        });
        if (!result && cancellation.cancelled()) {
          return grapple::foundation::Result<void>{};
        }
        return result ? grapple::foundation::Result<void>{} : result.error();
      }
    });
    if (!enqueue) {
      exportInProgress_ = false;
      activeExportJobId_.reset();
      appendError(enqueue.error());
      return false;
    }
    return true;
  }

  void waitForExportIdle() {
    workspace_.jobs().waitUntilIdle();
    refreshExportProgress();
    drainJobDispatch();
  }

  void drainJobDispatch() {
    refreshExportProgress();
    jobDispatcher_.drain();
  }

  void refreshExportProgress() {
    if (!activeExportJobId_.has_value()) {
      return;
    }
    const auto progressRecords = workspace_.jobs().progressRecords();
    for (auto record = progressRecords.rbegin(); record != progressRecords.rend(); ++record) {
      if (record->jobId != activeExportJobId_.value()) {
        continue;
      }
      const int percent = static_cast<int>(record->progress * 100.0);
      if (percent > lastLoggedExportProgressPercent_ &&
          (percent >= lastLoggedExportProgressPercent_ + 25 || percent == 100)) {
        lastLoggedExportProgressPercent_ = percent;
        log_->append(QString{"Export progress %1%"}.arg(percent));
      }
      return;
    }
  }

  void completeExport(const grapple::foundation::Result<grapple::render::FinalRenderResult>& result) {
    exportInProgress_ = false;
    activeExportJobId_.reset();
    if (!result) {
      appendError(result.error());
      return;
    }
    if (lastLoggedExportProgressPercent_ < 100) {
      lastLoggedExportProgressPercent_ = 100;
      log_->append("Export progress 100%");
    }
    appendDiagnostics(result.value());
    log_->append(QString{"Export evaluated %1 frames from %2 plan %3 -> %4"}
      .arg(result.value().framesEvaluated)
      .arg(qString(result.value().sourceRevision.value()))
      .arg(qString(result.value().renderPlanHash.toHex().substr(0, 8)))
      .arg(qString(result.value().outputPath.value)));
  }

  void savePackage() {
    const auto write = workspace_.writePackage();
    if (!write) {
      appendError(write.error());
      return;
    }
    appendPackageSaved(write.value());
  }

  void newPackageRoot(const grapple::foundation::FilePath& rootPath, std::string projectName) {
    pausePlayback();
    const auto created = workspace_.createPackageRootInPlace(rootPath, std::move(projectName));
    if (!created) {
      appendError(created.error());
      return;
    }
    const auto write = workspace_.writePackage();
    if (!write) {
      appendError(write.error());
      return;
    }

    selectedNodeId_ = std::nullopt;
    selectedAssetId_ = std::nullopt;
    refreshViewModelAndPreview();
    appendPackageSaved(write.value());
    log_->append(QString{"Created package %1"}.arg(qString(rootPath.value)));
  }

  void savePackageAs(const grapple::foundation::FilePath& rootPath) {
    const auto write = workspace_.savePackageAs(rootPath);
    if (!write) {
      appendError(write.error());
      return;
    }
    if (currentViewModel_.has_value()) {
      updateProjectHeader(currentViewModel_.value());
    }
    appendPackageSaved(write.value());
  }

  void openPackageRoot(const grapple::foundation::FilePath& rootPath) {
    pausePlayback();
    const auto opened = workspace_.openPackageRootInPlace(rootPath);
    if (!opened) {
      appendError(opened.error());
      return;
    }

    selectedNodeId_ = std::nullopt;
    selectedAssetId_ = std::nullopt;
    refreshViewModelAndPreview();
    log_->append(QString{"Opened package %1"}.arg(qString(rootPath.value)));
  }

private:
  void updateActionAvailability() {
    addSelectedMediaButton_->setEnabled(selectedAssetId_.has_value());
  }

  void updateProjectHeader(const grapple::app::AppViewModel& viewModel) {
    const grapple::storage::ProjectPackage& package = workspace_.project().packageState().package;
    const QString projectName = qString(viewModel.project.name);
    productTitle_->setText(QString{"%1  [%2]"}.arg(projectName, qString(viewModel.project.revision.value())));
    productSubtitle_->setText(QString{"%1  |  %2"}.arg(packageRootName(package.rootPath), qString(package.rootPath.value)));
    setWindowTitle(QString{"%1 - Grapple"}.arg(projectName));
  }

  void appendPackageSaved(const grapple::app::NativeWorkspaceWriteResult& write) {
    log_->append(QString{"Package saved\n%1\n%2\n%3\n%4\n%5\n%6\n%7"}
      .arg(qString(write.project.snapshotPath.value))
      .arg(qString(write.project.manifestPath.value))
      .arg(qString(write.project.commandLogPath.value))
      .arg(qString(write.project.eventLogPath.value))
      .arg(qString(write.project.schemaMigrationLogPath.value))
      .arg(qString(write.agentRunsPath.value))
      .arg(qString(write.agentEventsPath.value)));
  }

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
    updateActionAvailability();

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    updateSelectionPanels(viewModel.value());
  }

  void updateInspector(const grapple::app::AppViewModel& viewModel) {
    inspector_->setPlainText(inspectorText(viewModel, selectedNodeId_, selectedAssetId_, workspace_.preview().state().playhead));
    cameraProperties_->setSelection(viewModel, selectedNodeId_);
    clipTransform_->setSelection(viewModel, selectedNodeId_);
    effectParams_->setSelection(viewModel, selectedNodeId_, workspace_.preview().state().playhead);
    selectRelevantDetailTab(viewModel);
  }

  void selectRelevantDetailTab(const grapple::app::AppViewModel& viewModel) {
    if (detailTabs_ == nullptr) {
      return;
    }

    if (!selectedNodeId_.has_value()) {
      detailTabs_->setCurrentWidget(inspector_);
      return;
    }

    const grapple::foundation::NodeId selectedNodeId = selectedNodeId_.value();
    if (targetHasEditableEffects(viewModel, selectedNodeId)) {
      detailTabs_->setCurrentWidget(effectParamsScroll_);
      return;
    }

    const bool selectedCamera = std::any_of(
      viewModel.timeline.cameras.begin(),
      viewModel.timeline.cameras.end(),
      [&](const grapple::app::AppCameraRow& camera) {
        return camera.sourceNodeId == selectedNodeId;
      }
    );
    if (selectedCamera) {
      detailTabs_->setCurrentWidget(cameraProperties_);
      return;
    }

    const bool selectedClip = std::any_of(
      viewModel.timeline.clips.begin(),
      viewModel.timeline.clips.end(),
      [&](const grapple::app::AppClipRow& clip) {
        return clip.sourceNodeId == selectedNodeId;
      }
    );
    if (selectedClip) {
      detailTabs_->setCurrentWidget(clipTransform_);
    } else {
      detailTabs_->setCurrentWidget(inspector_);
    }
  }

  void updateSelectionPanels(const grapple::app::AppViewModel& viewModel) {
    updateInspector(viewModel);
    steward_->setViewModel(viewModel, workspace_.steward().conversationState(), selectedNodeId_, selectedAssetId_);
  }

  void refreshPlayheadEditControls() {
    if (currentViewModel_.has_value()) {
      refreshPlayheadEditControls(currentViewModel_.value());
      return;
    }

    const auto viewModel = workspace_.project().buildViewModel();
    if (!viewModel) {
      appendError(viewModel.error());
      return;
    }
    currentViewModel_ = viewModel.value();
    refreshPlayheadEditControls(currentViewModel_.value());
  }

  void refreshPlayheadEditControls(const grapple::app::AppViewModel& viewModel) {
    const grapple::foundation::TimeSeconds playhead = workspace_.preview().state().playhead;
    inspector_->setPlainText(inspectorText(viewModel, selectedNodeId_, selectedAssetId_, playhead));
    effectParams_->setSelection(viewModel, selectedNodeId_, playhead);
  }

  bool selectedPlayheadSensitivePanelVisible() const {
    if (detailTabs_ == nullptr) {
      return false;
    }
    QWidget* current = detailTabs_->currentWidget();
    return current == effectParamsScroll_ || current == inspector_;
  }

  bool effectParamsVisible() const {
    return detailTabs_ != nullptr && detailTabs_->currentWidget() == effectParamsScroll_;
  }

  bool selectedTargetHasAnimatedEffectParams(const grapple::app::AppViewModel& viewModel) const {
    if (!selectedNodeId_.has_value()) {
      return false;
    }

    for (const grapple::app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
      if (graph.targetNodeId != selectedNodeId_.value()) {
        continue;
      }
      for (const grapple::app::AppEffectRow& effect : graph.effects) {
        for (const grapple::app::AppEffectParamRow& param : effect.params) {
          if (!param.keyframes.empty()) {
            return true;
          }
        }
      }
    }
    return false;
  }

  void refreshPlaybackEditControlsIfNeeded() {
    if (workspace_.preview().state().playback != grapple::render::PreviewPlaybackState::Playing ||
        !currentViewModel_.has_value() ||
        !selectedPlayheadSensitivePanelVisible() ||
        !selectedTargetHasAnimatedEffectParams(currentViewModel_.value())) {
      return;
    }

    const grapple::foundation::TimeSeconds playhead = workspace_.preview().state().playhead;
    inspector_->setPlainText(inspectorText(currentViewModel_.value(), selectedNodeId_, selectedAssetId_, playhead));
    if (effectParamsVisible()) {
      effectParams_->refreshPlayheadValues(currentViewModel_.value(), selectedNodeId_, playhead);
    }
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
    updateActionAvailability();

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

  void chooseAndNewPackage() {
    const QString path = QFileDialog::getExistingDirectory(this, "New Package");
    if (path.isEmpty()) {
      return;
    }
    const std::string projectName = std::filesystem::path{path.toStdString()}.filename().string();
    newPackageRoot(grapple::foundation::FilePath{path.toStdString()}, projectName);
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

  void chooseAndSavePackageAs() {
    const QString path = QFileDialog::getExistingDirectory(this, "Save Package As");
    if (path.isEmpty()) {
      return;
    }
    savePackageAs(grapple::foundation::FilePath{path.toStdString()});
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
  QScrollArea* effectParamsScroll_ = nullptr;
  grapple::ui::ExportSettingsPanel* exportSettings_ = nullptr;
  grapple::ui::StewardPanel* steward_ = nullptr;
  QTabWidget* detailTabs_ = nullptr;
  QLabel* productTitle_ = nullptr;
  QLabel* productSubtitle_ = nullptr;
  QTextEdit* log_ = nullptr;
  QFrame* previewFrame_ = nullptr;
  QFrame* viewportFrame_ = nullptr;
  QPushButton* addSelectedMediaButton_ = nullptr;
  QTimer* playbackTimer_ = nullptr;
  QTimer* jobDispatchTimer_ = nullptr;
  grapple::jobs::MainThreadDispatcher jobDispatcher_;
  bool exportInProgress_ = false;
  int exportJobCounter_ = 0;
  std::optional<grapple::foundation::JobId> activeExportJobId_;
  int lastLoggedExportProgressPercent_ = -25;
  grapple::foundation::TimeSeconds timelineDuration_;
  grapple::ui::ExportSettingsDraft exportSettingsDraft_;
  std::optional<grapple::foundation::RevisionId> currentProjectRevision_;
  std::optional<grapple::app::AppViewModel> currentViewModel_;
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

std::string DesktopWindow::projectHeaderText() const {
  return impl_->projectHeaderText();
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

std::string DesktopWindow::stewardPrimaryActionText() const {
  return impl_->stewardPrimaryActionText();
}

bool DesktopWindow::stewardPrimaryActionEnabled() const {
  return impl_->stewardPrimaryActionEnabled();
}

std::string DesktopWindow::stewardSelectedClipActionText() const {
  return impl_->stewardSelectedClipActionText();
}

bool DesktopWindow::stewardSelectedClipActionEnabled() const {
  return impl_->stewardSelectedClipActionEnabled();
}

bool DesktopWindow::addSelectedMediaActionEnabled() const {
  return impl_->addSelectedMediaActionEnabled();
}

int DesktopWindow::stewardRecentEditCount() const {
  return impl_->stewardRecentEditCount();
}

int DesktopWindow::stewardCurrentRecentEditRow() const {
  return impl_->stewardCurrentRecentEditRow();
}

std::string DesktopWindow::stewardRecentEditText(int row) const {
  return impl_->stewardRecentEditText(row);
}

std::string DesktopWindow::effectParamTitleText() const {
  return impl_->effectParamTitleText();
}

std::string DesktopWindow::effectParamPanelText() const {
  return impl_->effectParamPanelText();
}

bool DesktopWindow::effectParamControlVisible(const std::string& paramName) const {
  return impl_->effectParamControlVisible(paramName);
}

std::optional<double> DesktopWindow::effectParamControlValue(const std::string& paramName) const {
  return impl_->effectParamControlValue(paramName);
}

std::string DesktopWindow::currentDetailTabText() const {
  return impl_->currentDetailTabText();
}

std::string DesktopWindow::stewardIntent() const {
  return impl_->stewardIntent();
}

void DesktopWindow::setStewardIntent(std::string intent) {
  impl_->setStewardIntent(std::move(intent));
}

void DesktopWindow::pressStewardSubmitShortcut() {
  impl_->pressStewardSubmitShortcut();
}

void DesktopWindow::clickStewardPrimaryAction() {
  impl_->clickStewardPrimaryAction();
}

void DesktopWindow::clickStewardSelectedClipAction() {
  impl_->clickStewardSelectedClipAction();
}

void DesktopWindow::clickStewardRecentEdit(int row) {
  impl_->clickStewardRecentEdit(row);
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

void DesktopWindow::newPackageRoot(const foundation::FilePath& rootPath, std::string projectName) {
  impl_->newPackageRoot(rootPath, std::move(projectName));
}

void DesktopWindow::savePackageAs(const foundation::FilePath& rootPath) {
  impl_->savePackageAs(rootPath);
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

void DesktopWindow::setEffectParamControlDraftValue(const std::string& paramName, double value) {
  impl_->setEffectParamControlDraftValue(paramName, value);
}

void DesktopWindow::setEffectParamControlValue(const std::string& paramName, double value) {
  impl_->setEffectParamControlValue(paramName, value);
}

void DesktopWindow::setEffectParamSliderRatio(const std::string& paramName, double ratio) {
  impl_->setEffectParamSliderRatio(paramName, ratio);
}

void DesktopWindow::setEffectParamKeyframeAtPlayhead(const std::string& paramName) {
  impl_->setEffectParamKeyframeAtPlayhead(paramName);
}

std::string DesktopWindow::effectParamKeyframeButtonText(const std::string& paramName) const {
  return impl_->effectParamKeyframeButtonText(paramName);
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
