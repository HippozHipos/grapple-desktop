#include <DemoProject.hpp>

#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/app/NativeProjectSession.hpp>

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPixmap>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <iostream>
#include <optional>
#include <string>

namespace {

void printError(const grapple::foundation::Error& error) {
  std::cerr << error.code << ": " << error.message << '\n';
}

QString qString(const std::string& value) {
  return QString::fromStdString(value);
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

QString timelineText(const grapple::app::AppViewModel& viewModel) {
  QString text;
  for (const grapple::app::AppCompositionRow& composition : viewModel.timeline.compositions) {
    text += QString{"Composition %1  %2\n"}
      .arg(qString(composition.sourceNodeId.value()))
      .arg(qString(composition.name));
  }
  for (const grapple::app::AppLayerRow& layer : viewModel.timeline.layers) {
    text += QString{"Layer %1  clips=%2\n"}
      .arg(qString(layer.name))
      .arg(layer.clipCount);
  }
  for (const grapple::app::AppClipRow& clip : viewModel.timeline.clips) {
    text += QString{"Clip %1  asset=%2  %3-%4s\n"}
      .arg(qString(clip.sourceNodeId.value()))
      .arg(qString(clip.assetId.value()))
      .arg(clip.timelineRange.start.value)
      .arg(clip.timelineRange.end.value);
  }
  for (const grapple::app::AppCameraRow& camera : viewModel.timeline.cameras) {
    text += QString{"Camera %1  %2\n"}
      .arg(qString(camera.sourceNodeId.value()))
      .arg(qString(camera.name));
  }
  return text;
}

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
    previewOutput_ = new QLabel;
    previewOutput_->setObjectName("previewOutput");
    previewOutput_->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(previewTitle_);
    previewLayout->addWidget(previewOutput_, 1);

    timeline_ = new QTextEdit;
    timeline_->setObjectName("timeline");
    timeline_->setReadOnly(true);

    log_ = new QTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);

    auto* refreshButton = new QPushButton{"Refresh Preview"};
    auto* addTrackButton = new QPushButton{"Add Track"};
    auto* exportButton = new QPushButton{"Export Smoke"};
    auto* saveButton = new QPushButton{"Save Package"};
    auto* actionColumn = new QVBoxLayout;
    actionColumn->addWidget(refreshButton);
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
    layout->addWidget(timeline_, 1, 0, 1, 1);
    layout->addWidget(log_, 1, 2, 1, 1);
    layout->setColumnStretch(0, 2);
    layout->setColumnStretch(1, 4);
    layout->setColumnStretch(2, 2);
    setCentralWidget(root);

    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshPreview(); });
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
      QLabel#previewOutput { color: #d8f3ff; font-size: 22px; font-weight: 700; }
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
    timeline_->setPlainText(timelineText(viewModel.value()));
  }

  void refreshPreview() {
    const auto refresh = preview_.refreshFromProject();
    if (!refresh) {
      appendError(refresh.error());
      return;
    }
    const auto frame = preview_.renderFrame(grapple::render::RenderFrameRequest{
      grapple::foundation::TimeSeconds{0.0},
      grapple::render::RenderQuality::Draft
    });
    if (!frame) {
      appendError(frame.error());
      return;
    }
    previewOutput_->setText(qString(frame.value().frame.description));
    log_->append(QString{"Preview refreshed at %1"}.arg(qString(refresh.value().revision.value())));
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
  QLabel* previewOutput_ = nullptr;
  QTextEdit* timeline_ = nullptr;
  QTextEdit* log_ = nullptr;
  QFrame* previewFrame_ = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
  bool smoke = false;
  bool mutateSmoke = false;
  std::optional<std::string> screenshotPath;
  for (int index = 1; index < argc; ++index) {
    const std::string argument{argv[index]};
    if (argument == "--smoke") {
      smoke = true;
    } else if (argument == "--mutate-smoke") {
      mutateSmoke = true;
    } else if (argument == "--screenshot" && index + 1 < argc) {
      screenshotPath = argv[++index];
    } else {
      std::cerr << "Expected --smoke, --mutate-smoke, or --screenshot <path>.\n";
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
